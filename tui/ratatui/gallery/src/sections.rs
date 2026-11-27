//! Every gallery section's specimens, in spec/gallery.md order, at the cell sizes
//! spec/terminal.md lists. A section is the page frame (2 columns, 1 row of padding, the title, a
//! blank row) around its body.

use omarchy_ratatui::layout::{Blank, Col, centered, col, custom, filled, row, sized};
use omarchy_ratatui::paint::{self, Ink};
use omarchy_ratatui::ratatui::layout::Rect;
use omarchy_ratatui::ui::*;
use omarchy_ratatui::{Draw, Theme};

use crate::state::{MODULES, NOTIFY, State, THEMES};

/// The sections, in order: (name, title).
pub const SECTIONS: &[(&str, &str)] = &[
    ("buttons", "Buttons"),
    ("toggles", "Toggles"),
    ("inputs", "Inputs"),
    ("slider", "Slider"),
    ("surfaces", "Surfaces"),
    ("lists", "Lists"),
    ("tiles", "Tiles"),
    ("progress", "Progress"),
    ("messages", "Messages"),
    ("menus", "Menus"),
    ("navigation", "Navigation"),
    ("forms", "Forms"),
    ("pickers", "Pickers"),
    ("data", "Data"),
    ("layout", "Layout"),
    ("timeline", "Timeline"),
    ("color", "Color"),
    ("composed", "Composed"),
    ("tokens", "Tokens"),
];

/// Every section is this wide; the body gets 4 fewer.
pub const COLS: u16 = 96;
pub const CONTENT: u16 = COLS - 4;

fn g(cp: u32) -> String {
    char::from_u32(cp).map(String::from).unwrap_or_default()
}

/// The page frame: padding 2 × 1, the section title (a SectionHeader), a blank row, the body.
pub fn page(s: &State, t: &Theme, name: &str) -> Col {
    let title = SECTIONS.iter().find(|(n, _)| *n == name).map_or(name, |(_, t)| t);
    let body = body(s, t, name);
    col(0).child(Blank(COLS, 1)).child(row(0).child(Blank(2, 1)).child(col(1).child(section_header(title)).boxed_child(body))).child(Blank(COLS, 1))
}

trait BoxedChild {
    fn boxed_child(self, d: Box<dyn Draw>) -> Self;
}

impl BoxedChild for Col {
    fn boxed_child(self, d: Box<dyn Draw>) -> Self {
        self.children([d])
    }
}

fn body(s: &State, t: &Theme, name: &str) -> Box<dyn Draw> {
    match name {
        "buttons" => Box::new(buttons(s)),
        "toggles" => Box::new(toggles(s)),
        "inputs" => Box::new(inputs(s)),
        "slider" => Box::new(slider(s)),
        "surfaces" => Box::new(surfaces(s)),
        "lists" => Box::new(lists(s)),
        "tiles" => Box::new(tiles(s)),
        "progress" => Box::new(progress(s)),
        "messages" => Box::new(messages(s)),
        "menus" => Box::new(menus(s)),
        "navigation" => Box::new(navigation(s)),
        "forms" => Box::new(forms(s)),
        "pickers" => Box::new(pickers(s)),
        "data" => Box::new(data(s)),
        "layout" => Box::new(layout(s)),
        "timeline" => Box::new(timeline(s)),
        "color" => Box::new(color(s, t)),
        "composed" => Box::new(composed(s)),
        _ => Box::new(tokens()),
    }
}

fn hot(s: &State, key: &str) -> bool {
    s.focus == Some(key)
}

/// The caret of a focused text field: at the end of its text (the gallery's fields edit there).
fn caret(s: &State, key: &str, text: &str) -> Option<usize> {
    hot(s, key).then(|| text.chars().count())
}

fn buttons(s: &State) -> Col {
    col(1)
        .child(
            row(1)
                .child(Button::new("Plain").cursor(hot(s, "plain")).key("plain"))
                .child(Button::new("Bordered").bordered(true).cursor(hot(s, "bordered")).key("bordered"))
                .child(Button::new("Hover").bordered(true).cursor(true))
                .child(Button::new("Selected").bordered(true).selected(true))
                .child(Button::new("Icon").icon(g(0xF013)).bordered(true).cursor(hot(s, "icon")).key("icon"))
                .child(IconButton::new(g(0xF011)).bordered(true).cursor(hot(s, "power")).key("power"))
                .child(IconButton::new(g(0xF021)).bordered(true).cursor(true)),
        )
        .child(ButtonGroup::new(["Left", "Center", "Right"], s.align).cursor(hot(s, "align").then_some(s.align)).key("align"))
}

fn toggles(s: &State) -> Col {
    let mut switches = row(2);
    for (i, on) in s.switches.iter().enumerate() {
        let cursor = i >= 2 || hot(s, ["switch-0", "switch-1", "switch-2", "switch-3"][i]);
        switches = switches.child(ToggleSwitch::new(*on).cursor(cursor).key(format!("switch-{i}")));
    }
    col(1)
        .child(switches)
        .child(Toggle::new("Wi-Fi").description("Connected to Basecamp").on(s.wifi).width(45).cursor(hot(s, "wifi")).key("wifi"))
        .child(Toggle::new("Bluetooth").description("Off").on(s.bluetooth).width(45).cursor(true).key("bluetooth"))
}

fn inputs(s: &State) -> Col {
    col(1)
        .child(
            row(1)
                .child(TextField::new(s.search.clone()).placeholder("Search…").width(28).cursor(hot(s, "search")).caret(caret(s, "search", &s.search)).key("search"))
                .child(TextField::new(s.name.clone()).width(28).cursor(hot(s, "name")).caret(caret(s, "name", &s.name)).key("name"))
                .child(TextField::new("Hovered").width(28).cursor(true)),
        )
        .child(
            row(2)
                .child(NumberField::new(s.gaps).label("Gaps").range(0, 40).width(15).cursor(hot(s, "gaps")).key("gaps"))
                .child(Dropdown::new(THEMES[s.theme_choice]).label("Theme").width(30).cursor(hot(s, "theme")).key("theme")),
        )
}

fn slider(s: &State) -> Col {
    col(1).child(Slider::new(s.level).width(45).cursor(hot(s, "level")).key("level")).child(Slider::new(s.stepped).notches(5).width(45).cursor(hot(s, "stepped")).key("stepped"))
}

fn surfaces(s: &State) -> Col {
    let card = Card::new()
        .title("Network")
        .width(38)
        .child(Toggle::new("Wi-Fi").description("Basecamp").on(s.network_on).width(32).cursor(hot(s, "network")).key("network"))
        .child(Separator(32))
        .child(col(0).child(MenuRow::new("Basecamp").checked(true).width(32)).child(MenuRow::new("Basecamp Guest").highlighted(true).width(32)).child(MenuRow::new("Coffee Shop").width(32)));
    let right = col(1)
        .child(Tooltip::new("Restart Wi-Fi"))
        .child(filled(43, 11, ConfirmDialog::new("Remove the Coffee Shop network?").width(39).selected(s.confirm_selected).key("confirm")));
    let layers = Pane::new()
        .title("Layers")
        .subtitle("3 items")
        .divider(Side::Left)
        .action(IconButton::new(g(0xF067)).cursor(hot(s, "add-layer")).key("add-layer"))
        .width(30)
        .child(col(0).child(MenuRow::new("Background").width(25)).child(MenuRow::new("Title").checked(true).width(25)).child(MenuRow::new("Logo").width(25)));
    let night = Pane::new()
        .title("Night light")
        .subtitle("Sunset to sunrise")
        .bordered(true)
        .action(ToggleSwitch::new(s.night_light).cursor(hot(s, "night-light")).key("night-light"))
        .width(30)
        .child(SettingRow::new("Strength").control(Slider::new(0.6).width(13)).width(24));
    let headings = row(2)
        .child(
            col(1)
                .child(Heading::new("Settings").subtitle("Omarchy").level(HeadingLevel::Display).width(30))
                .child(Heading::new("Appearance").subtitle("Theme, font and rounding").level(HeadingLevel::Heading).width(30)),
        )
        .child(col(1).child(Heading::new("Frame").trailing(IconButton::new(g(0xF141))).width(30)).child(Heading::new("notes.md").subtitle("~/Documents").centered(true).width(30)));
    col(1).child(row(2).child(card).child(right)).child(row(2).child(layers).child(night)).child(headings)
}

fn lists(s: &State) -> Col {
    let w = 53;
    let lock = g(0xF033E);
    let cur = |i: usize| s.rows.is_at("", i);
    let rows: Vec<Box<dyn Draw>> = vec![
        Box::new(ListRow::new("Coffee Shop").glyph(g(0xF0928)).subtitle("WPA2").value("5 GHz").trailing_glyph(lock.clone(), Tone::Neutral).cursor(cur(0)).current(s.current_row == 0).width(w).key("row-0")),
        Box::new(ListRow::new("Basecamp Guest").glyph(g(0xF0925)).subtitle("Open").cursor(cur(1)).current(s.current_row == 1).width(w).key("row-1")),
        Box::new(ListRow::new("Basecamp").glyph(g(0xF0928)).status("Connected", Tone::Success).cursor(cur(2)).current(s.current_row == 2).width(w).key("row-2")),
        Box::new(ListRow::new("Neighbor 5G").glyph(g(0xF091F)).subtitle("Out of range").disabled(true).width(w).key("row-3")),
        Box::new(
            ListRow::new("Messages")
                .leading(Avatar::new("Messages").size(AvatarSize::Small))
                .subtitle("From Basecamp")
                .trailing(Badge::new("3").tone(Tone::Accent).filled(true))
                .cursor(cur(4))
                .current(s.current_row == 4)
                .width(w)
                .key("row-4"),
        ),
        Box::new(ListRow::new("Headphones").glyph(g(0xF00AF)).subtitle("Battery 80%").trailing(ToggleSwitch::new(true).bare()).cursor(cur(5)).current(s.current_row == 5).width(w).key("row-5")),
        Box::new(ListRow::new("Office").glyph(g(0xF0922)).subtitle("Saved").trailing_glyph(lock.clone(), Tone::Neutral).action(g(0xF0159), true).action_cursor(true).cursor(cur(6)).current(s.current_row == 6).width(w).key("row-6")),
        Box::new(ListRow::new("Library").glyph(g(0xF0922)).subtitle("WPA2").trailing_glyph(lock.clone(), Tone::Neutral).action(g(0xF0159), true).cursor(cur(7)).current(s.current_row == 7).width(w).key("row-7")),
        Box::new(ListRow::new("Airport Free WiFi").glyph(g(0xF091F)).subtitle("Open").cursor(cur(8)).current(s.current_row == 8).width(w).key("row-8")),
    ];
    let mut area = ScrollArea::new(w).max_rows(7).offset(s.rows_offset);
    for r in rows {
        area = area.boxed(r);
    }
    let chevron = g(0xF0142);
    let stays = col(0)
        .child(ListRow::new("Appearance").glyph(g(0xF03D8)).subtitle("Theme, font, rounding").trailing_glyph(chevron.clone(), Tone::Neutral).width(w))
        .child(ListRow::new("Display").glyph(g(0xF0379)).subtitle("2560 × 1440").trailing_glyph(chevron, Tone::Neutral).width(w))
        .child(ListRow::new("Headphones").glyph(g(0xF02CB)).subtitle("Output").current(true).trailing_glyph(g(0xF012C), Tone::Accent).width(w));
    col(1)
        .child(area)
        .child(stays)
        .child(Hero::new(g(0xF0928), "Wi-Fi").status("Connected to Basecamp").trailing(ToggleSwitch::new(true)).width(w))
        .child(Hero::new(g(0xF00B2), "Bluetooth").status("No adapter found").error(true).glyph_opacity(0.5).width(w))
        .child(
            KeyValue::new()
                .columns(2)
                .pair("Ping", "18 ms")
                .pair_toned("Packet loss", "4%", Tone::Urgent)
                .pair("Receiving", "1.2 MB/s")
                .pair("Sending", "240 KB/s")
                .pair("IP address", "192.168.1.24")
                .pair("Gateway", "192.168.1.1"),
        )
}

fn tiles(s: &State) -> Col {
    let pic = || s.picture.clone();
    let badges = row(1)
        .child(Badge::new("New"))
        .child(Badge::new("3").tone(Tone::Accent).filled(true))
        .child(Badge::new("12").tone(Tone::Urgent).filled(true))
        .child(Badge::new("Online").tone(Tone::Success))
        .child(Badge::new("Low").tone(Tone::Warning))
        .child(Badge::new("Offline").tone(Tone::Urgent));
    let dots = row(1).child(StatusDot(Tone::Success)).child(StatusDot(Tone::Warning)).child(StatusDot(Tone::Urgent)).child(StatusDot(Tone::Muted)).child(StatusDot(Tone::Accent));
    let docked = row(2)
        .child(dots)
        .child(Avatar::new("Ryan").round(true).status(Tone::Success))
        .child(Avatar::new("Picture").image(pic()).status(Tone::Warning))
        .child(Avatar::new("").glyph(g(0xF048B)).status(Tone::Urgent));
    let avatars = row(2)
        .child(Avatar::new("ada").size(AvatarSize::Small))
        .child(Avatar::new("Basecamp"))
        .child(Avatar::new("Omarchy").size(AvatarSize::Large).round(true))
        .child(Avatar::new("").glyph(g(0xF06A9)))
        .child(Avatar::new("Picture").image(pic()).size(AvatarSize::Small))
        .child(Avatar::new("Picture").image(pic()).round(true))
        .child(Avatar::new("Picture").image(pic()).size(AvatarSize::Large))
        .child(Avatar::new("Missing").image(None));
    let by_name = row(2)
        .child(Avatar::new("Files").icon("org.gnome.Nautilus"))
        .child(Avatar::new("Terminal").icon("utilities-terminal"))
        .child(Avatar::new("Firefox").icon("firefox"))
        .child(Avatar::new("Chromium").icon("chromium").size(AvatarSize::Small))
        .child(Avatar::new("Steam").icon("steam").size(AvatarSize::Large))
        .child(Avatar::new("Btop").icon("btop").size(AvatarSize::Small))
        .child(Avatar::new("Nothing").icon("no-such-app").glyph(g(0xF120)));
    let grid = TileGrid::new([
        Tile::new("Terminal").glyph(g(0xF120)),
        Tile::new("Files").glyph(g(0xF07B)),
        Tile::new("Browser").glyph(g(0xF0AC)),
        Tile::new("Music").glyph(g(0xF001)),
        Tile::new("Settings").glyph(g(0xF013)),
        Tile::new("Calendar").glyph(g(0xF073)),
        Tile::new("Screen Recorder").glyph(g(0xF03D)),
        Tile::new("Photos").avatar(Avatar::new("Photos").image(pic())),
    ])
    .columns(4)
    .cursor(s.tile_cursor)
    .key("tile");
    col(1).child(badges).child(docked).child(avatars).child(by_name).child(grid)
}

fn progress(_: &State) -> Col {
    col(1)
        .child(
            row(2)
                .child(Meter::new(0.2).markers([0.5, 0.8]).width(28))
                .child(Meter::new(0.65).markers([0.5, 0.8]).width(28))
                .child(Meter::new(0.92).markers([0.5, 0.8]).alarming(true).width(28)),
        )
        .child(Meter::new(0.65).label("Battery").value_text("65%").width(45))
        .child(row(2).child(ProgressBar::new(0.4).width(43)).child(ProgressBar::indeterminate().width(43)))
        .child(row(2).child(Spinner::new()).child(Spinner::new()).child(Spinner::new().pulse(true)).child(Spinner::new().pulse(true)))
}

fn messages(s: &State) -> Col {
    let boxes = row(2)
        .child(centered(44, 9, EmptyState::new("No networks found").glyph(g(0xF05AA)).hint("Turn on Wi-Fi to scan").action("Scan").cursor(hot(s, "scan")).width(44)))
        .child(centered(44, 9, EmptyState::new("Clipboard is empty").glyph(g(0xF014C)).hint("Copy something to see it here").overlay(true).width(44)));
    col(1)
        .child(boxes)
        .child(
            col(1)
                .child(Banner::new("Omarchy 3.2 is available").tone(Tone::INFO).action("Update").dismissible(true).width(CONTENT))
                .child(Banner::new("Backup finished").tone(Tone::Success).dismissible(true).width(CONTENT))
                .child(Banner::new("Battery is low (12%)").tone(Tone::Warning).dismissible(true).width(CONTENT))
                .child(Banner::new("Could not reach the update server").tone(Tone::ERROR).dismissible(true).width(CONTENT)),
        )
        .child(Toast::new("Export finished").text("omarchy-demo.mp4 · 42 MB").glyph(g(0xF012C)).action("Open").countdown(0.6).width(45))
}

pub const MENU_ITEMS: [&str; 7] = ["New", "Open…", "Save", "Duplicate", "Sidebar", "Word wrap", "Export"];
pub const CONTEXT_ITEMS: [&str; 6] = ["Cut", "Copy", "Paste", "By name", "By date", "Delete"];

fn menus(s: &State) -> Col {
    let mc = |i: usize| s.menu_cursor == i;
    let menu = Menu::new()
        .section("File")
        .item(MenuItem::new("New").icon(g(0xF15B)).shortcut("Ctrl+N").cursor(mc(0)).key("menu-new"))
        .item(MenuItem::new("Open…").icon(g(0xF07B)).shortcut("Ctrl+O").cursor(mc(1)).key("menu-open"))
        .item(MenuItem::new("Save").icon(g(0xF0C7)).shortcut("Ctrl+S").cursor(mc(2)).key("menu-save"))
        .item(MenuItem::new("Duplicate").icon(g(0xF0C5)).disabled(true).key("menu-duplicate"))
        .separator()
        .section("View")
        .item(MenuItem::new("Sidebar").check(s.sidebar_shown).cursor(mc(4)).key("menu-sidebar"))
        .item(MenuItem::new("Word wrap").check(s.word_wrap).cursor(mc(5)).key("menu-wrap"))
        .separator()
        .item(MenuItem::new("Export").icon(g(0xF064)).submenu(true).cursor(mc(6)).key("menu-export"));
    let cc = |i: usize| hot(s, "context") && s.context_cursor == i;
    let context = Menu::new()
        .item(MenuItem::new("Cut").shortcut("Ctrl+X").cursor(cc(0)).key("context-cut"))
        .item(MenuItem::new("Copy").shortcut("Ctrl+C").cursor(cc(1)).key("context-copy"))
        .item(MenuItem::new("Paste").shortcut("Ctrl+V").cursor(cc(2)).key("context-paste"))
        .separator()
        .item(MenuItem::new("By name").radio(s.sort_by_name).cursor(cc(3)).key("context-name"))
        .item(MenuItem::new("By date").radio(!s.sort_by_name).cursor(cc(4)).key("context-date"))
        .separator()
        .item(MenuItem::new("Delete").shortcut("Del").destructive(true).cursor(cc(5)).key("context-delete"));
    let keys = ["Super", "Ctrl", "Alt", "Shift", "Tab", "Esc", "Enter", "Backspace", "Up", "Space", "K"];
    let mut caps = row(1);
    for k in keys {
        caps = caps.child(Keycap::new(k));
    }
    let sheet = ShortcutSheet::new()
        .group("File", [("Ctrl+N", "New file"), ("Ctrl+O", "Open"), ("Ctrl+S", "Save")])
        .group("View", [("Ctrl+B", "Sidebar"), ("Ctrl+Shift+P", "Commands"), ("F11", "Full screen")])
        .columns(2);
    col(1).child(row(2).child(menu).child(context).child(Tooltip::new("Save").shortcut("Ctrl+S"))).child(caps).child(KeyCombo::new("Ctrl+Shift+P")).child(sheet)
}

pub const RAIL: [(u32, &str); 5] = [(0xF07B, "Files"), (0xF002, "Search"), (0xF126, "Source control"), (0xF188, "Debug"), (0xF013, "Settings")];

fn navigation(s: &State) -> Col {
    let ib = |cp: u32, cursor: bool| IconButton::new(g(cp)).cursor(cursor);
    let bar = Toolbar::new(CONTENT)
        .leading(ib(0xF032, false))
        .leading(ib(0xF033, true))
        .leading(ib(0xF0CD, false))
        .leading(toolbar_separator())
        .leading(ib(0xF0CA, false))
        .leading(ib(0xF0CB, false))
        .leading(toolbar_separator())
        .leading(ib(0xF03E, false))
        .leading(ib(0xF0C1, false))
        .center(Heading::new("notes.md").centered(true))
        .trailing(Button::new("Share").bordered(true))
        .overflow(true);
    let tabs = Tabs::new(["General", "Display", "Audio", "Network"], s.tab).cursor(s.tab_cursor).width(CONTENT);
    let rail = IconRail::new(RAIL.iter().map(|(cp, l)| (g(*cp), l.to_string())), s.panel).cursor(hot(s, "rail").then_some(s.panel));
    let name = RAIL[s.panel].1.to_string();
    let content = custom(40, 5, move |t: &Theme, area: Rect, buf| {
        paint::text_center(t, buf, area.x, area.y + (area.height - 1) / 2, area.width, &name, t.dim());
    });
    col(1).child(bar).child(tabs).child(row(2).child(rail).child(content))
}

fn forms(s: &State) -> Col {
    let settings = col(1)
        .child(SettingRow::new("Night light").description("Warm the screen after sunset").control(ToggleSwitch::new(s.night_toggle).cursor(hot(s, "night")).key("night")).width(60))
        .child(SettingRow::new("Theme").description("Applies to every app").control(Dropdown::new(THEMES[s.theme_choice]).width(23)).width(60))
        .child(SettingRow::new("Gaps").description("Space between windows").control(NumberField::new(s.gaps).range(0, 40).width(15)).width(60));
    let labels = ["Show seconds", "Show date", "Show weekday", "Show week number", "Twelve-hour clock"];
    let mut checks = col(0);
    for (i, l) in labels.iter().enumerate() {
        let mut c = Checkbox::new(*l).checked(s.checks[i]).cursor(i == 2 || hot(s, ["check-0", "check-1", "check-2", "check-3", "check-4"][i])).disabled(i == 3).key(format!("check-{i}"));
        if i == 4 {
            c = c.description("Show AM and PM");
        }
        checks = checks.child(c);
    }
    let radios = RadioGroup::new([("top", "Top"), ("bottom", "Bottom"), ("hidden", "Hidden")], s.position.clone()).cursor(hot(s, "radio").then(|| ["top", "bottom", "hidden"].iter().position(|v| *v == s.position).unwrap_or(0))).key("radio");
    let passwords = row(1)
        .child(PasswordField::new("").placeholder("Password").width(28))
        .child(PasswordField::new(s.password.clone()).revealable(true).revealed(s.revealed).cursor(hot(s, "password")).caret(caret(s, "password", &s.password)).width(28).key("password"))
        .child(PasswordField::new("letmein").error("Wrong password, try again").width(28));
    col(1)
        .child(settings)
        .child(row(4).child(checks).child(radios))
        .child(passwords)
        .child(TextArea::new("Back up your dotfiles\nbefore switching themes,\nthen run omarchy update.").width(60))
}

fn pickers(s: &State) -> Col {
    let bar = MultiSelect::new("modules", MODULES, s.modules.clone()).label("Bar modules").cursor(hot(s, "modules")).width(33);
    let notify = MultiSelect::new("notify", NOTIFY, s.notify.clone())
        .label("Notify for")
        .open(s.notify_state.open)
        .search(s.notify_state.search.clone())
        .highlighted(s.notify_state.row)
        .cursor(hot(s, "notify"))
        .width(33);
    // The wells' colors come from the theme at draw time, so they fade: draw them through a closure.
    let wells = custom(23, 1, move |t: &Theme, area: Rect, buf| {
        ColorWell::new(t.accent()).draw(t, Rect::new(area.x, area.y, 7, 1), buf);
        ColorWell::new(t.success()).cursor(true).draw(t, Rect::new(area.x + 8, area.y, 7, 1), buf);
        ColorWell::new(t.urgent()).draw(t, Rect::new(area.x + 16, area.y, 7, 1), buf);
    });
    let (swatch, swatch_cursor) = (s.swatch, s.swatch_cursor);
    let strip = custom(23, 2, move |t: &Theme, area: Rect, buf| {
        let sh = t.shown();
        SwatchStrip::new([t.accent(), t.urgent(), t.warning(), t.success(), t.fg(), t.dim(), t.faint(), sh.palette.muted], Some(swatch)).cursor(swatch_cursor).key("swatch").draw(t, area, buf);
    });
    let card = Card::new().title("Displays").width(53).child(col(0).child(MenuRow::new("Built-in display").checked(true).width(47)).child(MenuRow::new("External monitor").width(47)).child(MenuRow::new("Projector").width(47)));
    let dialog = Dialog::new("Discard changes?").message("Your edits to Tokyo Night will be lost.").labels("Keep editing", "Discard").destructive(true).selected(s.discard_selected).width(46).key("discard");
    let modal = custom(53, 13, move |t: &Theme, area: Rect, buf| {
        card.draw(t, area, buf);
        dialog.draw(t, area, buf);
    });
    col(1).child(row(2).child(bar).child(sized(33, 11, notify))).child(wells).child(strip).child(modal)
}

pub fn tree_nodes() -> Vec<TreeNode> {
    let file = || g(0xF0219);
    vec![TreeNode::new("root", "omarchy-ui").glyph(g(0xF0770)).children([
        TreeNode::new("qt", "qt").glyph(g(0xF0770)).children([TreeNode::new("qt/button", "Button.qml").glyph(file()), TreeNode::new("qt/table", "Table.qml").glyph(file())]),
        TreeNode::new("rune", "rune").glyph(g(0xF024B)).children([TreeNode::new("rune/lib", "lib.rs").glyph(file())]),
        TreeNode::new("spec", "spec").glyph(g(0xF0770)).children([TreeNode::new("spec/tokens", "tokens.md").glyph(file())]),
        TreeNode::new("readme", "README.md").glyph(file()),
    ])]
}

pub const TABLE_ROWS: [[&str; 4]; 6] = [
    ["assets", "Folder", "—", "2026-11-02"],
    ["Cargo.toml", "TOML", "1.2 KB", "2026-11-07"],
    ["README.md", "Markdown", "4.8 KB", "2026-11-05"],
    ["screenshot-of-the-new-settings-panel.png", "PNG image", "1.4 MB", "2026-11-08"],
    ["src", "Folder", "—", "2026-11-08"],
    ["theme.toml", "TOML", "812 B", "2026-10-30"],
];

fn data(s: &State) -> Col {
    let table = Table::new([Column::new("Name"), Column::new("Kind").width(11), Column::new("Size").width(9).right(), Column::new("Modified").width(13)])
        .rows(TABLE_ROWS)
        .sort(0, true)
        .selected(Some(s.table_selected))
        .cursor(Some(s.table_cursor))
        .width(60)
        .key("table");
    let tree = Tree::new(tree_nodes()).expanded(s.tree_expanded.clone()).selected(Some(&s.tree_selected)).cursor(Some(&s.tree_cursor)).width(28).key("tree");
    col(0).child(row(2).child(table).child(tree))
}

pub fn sidebar_sections() -> Vec<SidebarSection> {
    vec![
        SidebarSection::new(
            "Library",
            [SidebarItem::new("all", "All files", g(0xF024B)), SidebarItem::new("recent", "Recent", g(0xF02DA)), SidebarItem::new("starred", "Starred", g(0xF04CE)), SidebarItem::new("trash", "Trash", g(0xF0A7A))],
        ),
        SidebarSection::new("Places", [SidebarItem::new("documents", "Documents", g(0xF0219)), SidebarItem::new("downloads", "Downloads", g(0xF01DA))]),
    ]
}

fn layout(s: &State) -> Col {
    let side = Sidebar::new(sidebar_sections()).selected(Some(&s.sidebar_selected)).cursor(hot(s, "sidebar").then_some(s.sidebar_selected.as_str())).width(s.sidebar_width).key("side");
    let empty = custom(10, 1, |t: &Theme, area: Rect, buf| {
        paint::text_center(t, buf, area.x, area.y + (area.height.saturating_sub(1)) / 2, area.width, "No file selected", t.faint());
    });
    let split = SplitView::new(88, 12).pane(SplitPane::new(side).size(s.sidebar_width).min(15)).pane(SplitPane::new(empty)).cursor(hot(s, "handle").then_some(0));
    let framed = custom(90, 14, move |t: &Theme, area: Rect, buf| {
        let inner = paint::frame(t, buf, area, t.track(), None);
        split.draw(t, inner, buf);
    });
    let (y, m) = s.month;
    col(1).child(framed).child(Calendar::new(y, m).today(Date::new(2026, 11, 8)).selected(Some(s.day_selected)).cursor(s.day_cursor).key("cal"))
}

pub const DRAG_ROWS: [&str; 4] = ["Workspaces", "Clock", "Battery", "Network"];

fn timeline(s: &State) -> Col {
    let tl = Timeline::new(30.0)
        .position(s.playhead)
        .track([Clip::new(0.0, 7.5, "Intro"), Clip::new(8.0, 18.0, "Demo"), Clip::new(18.5, 30.0, "Outro")])
        .track([Clip::new(2.0, 12.0, "Voiceover"), Clip::new(14.0, 26.0, "Music")])
        .selected(s.clip)
        .cursor(hot(s, "timeline"))
        .size_cells(90, 10);
    let drag = s.drag_cursor;
    let list = custom(45, 5, move |t: &Theme, area: Rect, buf| {
        let mut y = area.y;
        for (i, l) in DRAG_ROWS.iter().enumerate() {
            if i == 3 {
                DropIndicator(area.width).draw(t, Rect::new(area.x, y, area.width, 1), buf);
                y += 1;
            }
            let r = Rect::new(area.x, y, area.width, 1);
            if i == drag {
                paint::fill(t, buf, r, t.shown().controls.hover.fill);
            }
            DragHandle::new().cursor(i == drag).draw(t, Rect::new(area.x + 1, y, 1, 1), buf);
            paint::text_fit(t, buf, area.x + 3, y, l, area.width - 4, t.fg());
            t.hit(format!("drag-{i}"), r);
            y += 1;
        }
    });
    let crop = custom(30, 9, |t: &Theme, area: Rect, buf| {
        paint::fill(t, buf, area, t.shown().controls.normal.fill);
        CropFrame::new(area.width, area.height).draw(t, area, buf);
    });
    col(1).child(tl).child(Scrubber::new(s.scrub, 130.0).width(45).cursor(hot(s, "scrubber"))).child(row(2).child(list).child(crop))
}

fn color(s: &State, _: &Theme) -> Col {
    col(0).child(ColorPicker::new(s.color).cursor(hot(s, "picker")).size_cells(38, 16))
}

pub fn search_items() -> Vec<SearchItem> {
    vec![
        SearchItem::new("Terminal").glyph(g(0xF120)).subtitle("App").detail("Open a new terminal window."),
        SearchItem::new("Terminal font").glyph(g(0xF031)).subtitle("Style").detail("Pick the monospace font every terminal uses."),
        SearchItem::new("Terminal theme").glyph(g(0xF1FC)).subtitle("Style").detail("Match the terminal colors to the desktop theme."),
        SearchItem::new("Intermediate CA").glyph(g(0xF0A3)).subtitle("Certificate").detail("Trust a certificate authority for this machine."),
    ]
}

fn composed(s: &State) -> Col {
    let w = 39;
    let lock = g(0xF033E);
    let wc = |i: usize| s.wifi_rows.is_at("", i);
    let expansion = row(1).child(PasswordField::new("").placeholder("Password").width(23)).child(Button::new("Connect").bordered(true));
    let networks = ScrollArea::new(w)
        .max_rows(18)
        .child(ListRow::new("Basecamp").glyph(g(0xF0928)).status("Connected", Tone::Success).current(true).cursor(wc(0)).width(w).key("wifi-0"))
        .child(ListRow::new("Basecamp Guest").glyph(g(0xF0925)).subtitle("Open").action(g(0xF0159), true).cursor(wc(1)).width(w).key("wifi-1"))
        .child(ListRow::new("Coffee Shop").glyph(g(0xF0922)).subtitle("WPA2").trailing_glyph(lock.clone(), Tone::Neutral).expansion(expansion).cursor(wc(2)).width(w).key("wifi-2"))
        .child(ListRow::new("Library").glyph(g(0xF091F)).subtitle("Saved").trailing_glyph(lock, Tone::Neutral).cursor(wc(3)).width(w).key("wifi-3"));
    let panel = Card::new()
        .width(45)
        .child(Hero::new(g(0xF0928), "Wi-Fi").status("Connected to Basecamp").trailing(ToggleSwitch::new(true)).width(w))
        .child(section_header("Networks"))
        .child(networks)
        .child(Separator(w))
        .child(KeyValue::new().pair("IP", "192.168.1.24").pair("Gateway", "192.168.1.1").pair("DNS", "1.1.1.1"));
    let iw = 40;
    let inspector = Pane::new()
        .title("Recording 1")
        .subtitle("1920 × 1080 · 0:42")
        .divider(Side::Left)
        .action(IconButton::new(g(0xF0E2)))
        .action(IconButton::new(g(0xF01E)))
        .action(Button::new("Export").bordered(true))
        .width(45)
        .child(Tabs::new(["Shape", "Cursor", "Zoom"], s.inspector_tab).width(iw))
        .child(
            col(0)
                .child(section_header("Frame"))
                .child(SettingRow::new("Radius").control(Slider::new(0.4).width(14)).width(iw))
                .child(SettingRow::new("Border").control(Dropdown::new("Solid").width(14)).width(iw))
                .child(SettingRow::new("Shadow").control(ToggleSwitch::new(true)).cursor(true).width(iw)),
        )
        .child(
            col(0)
                .child(section_header("Background"))
                .child(SettingRow::new("Fill").control(custom(7, 1, |t: &Theme, area: Rect, buf| ColorWell::new(t.accent()).draw(t, area, buf))).width(iw))
                .child(SettingRow::new("Padding").control(NumberField::new(24).range(0, 96).width(15)).width(iw)),
        );
    let items: Vec<SearchItem> = search_items().into_iter().filter(|i| SearchList::matches(&i.title, &s.query)).collect();
    let cursor = s.search_cursor.active.then_some(s.search_cursor.index());
    let search = SearchList::new("search", s.query.clone()).items(items).cursor(cursor).detail(true).size_cells(45, 12);
    let none: Vec<SearchItem> = search_items().into_iter().filter(|i| SearchList::matches(&i.title, "zzz")).collect();
    let empty = SearchList::new("search-empty", "zzz").items(none).size_cells(38, 9);
    col(1).child(row(2).child(panel).child(inspector)).child(row(2).child(search).child(empty))
}

fn tokens() -> Col {
    let names = ["foreground", "background", "accent", "urgent", "muted"];
    let swatches = custom(CONTENT, 3, move |t: &Theme, area: Rect, buf| {
        let p = &t.shown().palette;
        let colors = [p.foreground, p.background, p.accent, p.urgent, p.muted];
        for (i, (n, c)) in names.iter().zip(colors).enumerate() {
            let x = area.x + i as u16 * 13;
            // The swatch is its color's background, outlined in the normal border at its outer
            // columns, so the background swatch shows too.
            let border = t.shown().controls.normal.border.color;
            paint::fill(t, buf, Rect::new(x, area.y, 8, 2), c);
            for dy in 0..2 {
                paint::edges(t, buf, x, area.y + dy, 8, Some(border));
            }
            paint::text(t, buf, x, area.y + 2, n, 12, t.fg());
        }
    });
    let chips = custom(CONTENT, 1, |t: &Theme, area: Rect, buf| {
        let c = &t.shown().controls;
        let states = [
            ("normal", c.normal.fill, &c.normal.border, t.fg(), false),
            ("hover-cursor", c.hover.fill, &c.hover.border, t.fg(), false),
            ("focus", c.focus.fill, &c.focus.border, t.fg(), false),
            ("selected", c.selected.fill, &c.selected.border, c.selected.text, true),
        ];
        for (i, (n, f, b, ink, bold)) in states.into_iter().enumerate() {
            let x = area.x + i as u16 * 16;
            paint::fill(t, buf, Rect::new(x, area.y, 15, 1), f);
            if !b.is_none() {
                paint::edges(t, buf, x, area.y, 15, Some(b.color));
            }
            paint::text_center(t, buf, x + 1, area.y, 13, n, Ink::new(ink).bold(bold));
        }
    });
    let scale = custom(CONTENT, 8, |t: &Theme, area: Rect, buf| {
        let f = &t.shown().font;
        let rows = [
            ("displayLarge", f.display_large, true, t.fg()),
            ("display", f.display, true, t.fg()),
            ("heading", f.heading, true, t.fg()),
            ("title", f.title, true, t.fg()),
            ("subtitle", f.subtitle, false, t.fg()),
            ("body", f.body, false, t.fg()),
            ("bodySmall", f.body_small, false, t.fg()),
            ("caption", f.caption, false, t.dim()),
        ];
        for (i, (n, px, bold, ink)) in rows.into_iter().enumerate() {
            let y = area.y + i as u16;
            paint::text(t, buf, area.x, y, &format!("{n} {px}"), 16, t.dim());
            paint::text(t, buf, area.x + 16, y, "The quick brown fox jumps over the lazy dog", 60, Ink::new(ink).bold(bold));
        }
    });
    col(1).child(swatches).child(chips).child(scale)
}
