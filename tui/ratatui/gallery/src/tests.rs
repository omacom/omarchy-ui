//! Behavior tests: the keys and clicks the GUI galleries' tests drive (`qt/tests/tst_*.qml`, the
//! Rune gallery's tests), on the terminal gallery's state; the section sizes spec/terminal.md
//! lists; and snapshots in the shared canonical format.

use super::*;
use omarchy_ratatui::crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use omarchy_ratatui::ui::{Date, Dialog, SectionHeader, Spinner};

fn fixture(name: &str) -> Theme {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../fixtures/themes").join(name);
    let read = |f: &str| std::fs::read_to_string(dir.join(f)).unwrap_or_default();
    let mut t = Theme::from_tokens(omarchy_theme::resolve(&omarchy_theme::Sources {
        colors: omarchy_theme::parse::parse_colors(&read("colors.toml")),
        shell: omarchy_theme::parse::parse_shell(&read("shell.toml")),
        font_family: "monospace".into(),
        resolved_font_family: "monospace".into(),
        ..Default::default()
    }));
    t.set_depth(omarchy_ratatui::Depth::True);
    t
}

use omarchy_ratatui::omarchy_theme;

/// A section of the gallery with its state, drawn so its keys can be found and clicked.
struct Rt {
    t: Theme,
    s: State,
    section: &'static str,
}

impl Rt {
    fn new(section: &'static str) -> Rt {
        let mut rt = Rt { t: fixture("tokyo-night"), s: State::init(), section };
        rt.draw();
        rt
    }
    fn draw(&mut self) {
        self.t.clear_hits();
        let _ = draw_page(&self.s, &self.t, self.section);
    }
    fn find(&self, key: &str) -> Rect {
        self.t.find(key).unwrap_or_else(|| panic!("no {key} drawn in {}", self.section))
    }
    /// Click the middle of a keyed part, as a mouse press would.
    fn click(&mut self, key: &str) {
        self.draw();
        let r = self.find(key);
        let (col, row) = (r.x + r.width / 2, r.y + r.height / 2);
        let hit = self.t.hit_at(col, row).unwrap();
        if let Some(f) = interact::focus_of(self.section, &hit) {
            self.s.focus = Some(f);
        }
        interact::click(&mut self.s, &self.t, self.section, &hit, col, r);
        self.draw();
    }
    fn focus(&mut self, f: &'static str) {
        assert!(interact::focusables(self.section).contains(&f), "{f} is focusable in {}", self.section);
        self.s.focus = Some(f);
    }
    fn key(&mut self, k: &str) -> bool {
        let (code, mods) = match k {
            "Down" => (KeyCode::Down, KeyModifiers::NONE),
            "Up" => (KeyCode::Up, KeyModifiers::NONE),
            "Left" => (KeyCode::Left, KeyModifiers::NONE),
            "Right" => (KeyCode::Right, KeyModifiers::NONE),
            "Shift+Right" => (KeyCode::Right, KeyModifiers::SHIFT),
            "Home" => (KeyCode::Home, KeyModifiers::NONE),
            "End" => (KeyCode::End, KeyModifiers::NONE),
            "Enter" => (KeyCode::Enter, KeyModifiers::NONE),
            "Esc" => (KeyCode::Esc, KeyModifiers::NONE),
            "Space" => (KeyCode::Char(' '), KeyModifiers::NONE),
            "Backspace" => (KeyCode::Backspace, KeyModifiers::NONE),
            c => (KeyCode::Char(c.chars().next().unwrap()), KeyModifiers::NONE),
        };
        let r = interact::key(&mut self.s, &KeyEvent::new(code, mods));
        self.draw();
        r
    }
    fn keys(&mut self, ks: &[&str]) {
        for k in ks {
            self.key(k);
        }
    }
}

/// The natural size of every section, as spec/terminal.md lists it (96 columns wide).
const SIZES: &[(&str, u16)] = &[
    ("buttons", 7),
    ("toggles", 11),
    ("inputs", 8),
    ("slider", 7),
    ("surfaces", 34),
    ("lists", 25),
    ("tiles", 25),
    ("progress", 12),
    ("messages", 26),
    ("menus", 26),
    ("navigation", 15),
    ("forms", 26),
    ("pickers", 34),
    ("data", 12),
    ("layout", 27),
    ("timeline", 26),
    ("color", 20),
    ("composed", 37),
    ("tokens", 18),
];

/// Every section lays out at its spec'd size and draws in every fixture theme, at both radii.
#[test]
fn every_section_draws_at_its_size_in_every_theme() {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../fixtures/themes");
    let mut themes: Vec<String> = std::fs::read_dir(dir).unwrap().map(|e| e.unwrap().file_name().into_string().unwrap()).collect();
    themes.sort();
    assert_eq!(SECTIONS.len(), SIZES.len());
    for ((name, _), (n, rows)) in SECTIONS.iter().zip(SIZES) {
        assert_eq!(name, n, "sections in spec/gallery.md order");
        for theme in &themes {
            let t = fixture(theme);
            let s = State::init();
            let p = page(&s, &t, name);
            assert_eq!(p.size(&t), (COLS, *rows), "{name} in {theme}");
            let _ = draw_page(&s, &t, name);
        }
    }
}

/// A snapshot is exactly cols × rows of the shared canonical format, and writing it again from
/// its parse changes nothing.
#[test]
fn snapshots_are_canonical() {
    for (name, rows) in SIZES {
        let mut t = fixture("catppuccin-latte");
        let s = State::init();
        let p = page(&s, &t, name);
        let text = omarchy_ratatui::snap::render(&mut t, &p, COLS, 0);
        let g = omarchy_ansi::parse(&text).unwrap_or_else(|e| panic!("{name}: {e}"));
        assert_eq!((g.cols, g.rows), (COLS as usize, *rows as usize), "{name}");
        assert_eq!(omarchy_ansi::write_canonical(&g), text, "{name} is canonical");
        // The page background is the terminal's: never painted.
        assert!(g.cells.iter().filter(|c| c.text == " ").any(|c| c.bg == omarchy_ansi::Color::Default));
        // An explicit size crops or pads.
        let small = omarchy_ratatui::snap::render(&mut t, &p, 40, 5);
        let g = omarchy_ansi::parse(&small).unwrap();
        assert_eq!((g.cols, g.rows), (40, 5));
    }
}

#[test]
fn toggles_toggle() {
    let mut rt = Rt::new("toggles");
    rt.click("switch-0");
    assert!(rt.s.switches[0]);
    rt.click("bluetooth");
    assert!(rt.s.bluetooth);
    rt.focus("wifi");
    rt.key("Space");
    assert!(!rt.s.wifi);
    rt.key("Enter");
    assert!(rt.s.wifi);
}

#[test]
fn button_group_and_inputs_take_keys() {
    let mut rt = Rt::new("buttons");
    rt.focus("align");
    rt.keys(&["Right", "l"]);
    assert_eq!(rt.s.align, 2, "stops at the end");
    rt.keys(&["h", "Home"]);
    assert_eq!(rt.s.align, 0);
    rt.click("align-2");
    assert_eq!(rt.s.align, 2);

    let mut rt = Rt::new("inputs");
    rt.focus("search");
    rt.keys(&["w", "i", "f", "i", "Backspace"]);
    assert_eq!(rt.s.search, "wif");
    rt.click("gaps-inc");
    assert_eq!(rt.s.gaps, 11);
    rt.focus("gaps");
    rt.keys(&["Down", "Down"]);
    assert_eq!(rt.s.gaps, 9);
    // The dropdown opens on Return, walks with j/k, picks with Return.
    rt.focus("theme");
    rt.keys(&["Enter", "j", "j", "k", "Enter"]);
    assert_eq!(THEMES[rt.s.theme_choice], "Catppuccin");
    assert!(!rt.s.dropdown_open);
    // A click on the trigger opens it, a click on a row picks.
    rt.click("theme");
    assert!(rt.s.dropdown_open);
    rt.click("theme-list-3");
    assert_eq!(THEMES[rt.s.theme_choice], "Nord");
}

#[test]
fn sliders_step_and_follow_clicks() {
    let mut rt = Rt::new("slider");
    rt.focus("stepped");
    rt.key("Right");
    assert_eq!(rt.s.stepped, 1.0);
    rt.keys(&["Left", "Left"]);
    assert_eq!(rt.s.stepped, 0.5);
    let r = rt.find("level");
    interact::click(&mut rt.s, &rt.t, "slider", "level", r.x + 1, r);
    assert_eq!(rt.s.level, 0.0);
}

/// The list's cursor moves with the keys, reveals rows past the cap, activates; the pointer moves
/// it only when it reaches another cell (CursorModel's gate).
#[test]
fn lists_cursor_follows_keys_and_pointer() {
    let mut rt = Rt::new("lists");
    rt.focus("rows");
    assert!(rt.s.rows.is_at("", 1));
    rt.key("j");
    assert!(rt.s.rows.is_at("", 2));
    rt.key("k");
    assert!(rt.s.rows.is_at("", 1));
    rt.key("End");
    assert!(rt.s.rows.is_at("", 8));
    assert_eq!(rt.s.rows_offset, 2, "the last row is revealed");
    rt.key("Home");
    assert_eq!(rt.s.rows_offset, 0);
    rt.keys(&["j", "Enter"]);
    assert_eq!(rt.s.current_row, 1);
    // The pointer: the first sample primes, the next in another cell moves.
    let r = rt.find("row-4");
    assert!(!interact::hover(&mut rt.s, "row-4", r.x + 3, r.y));
    assert!(interact::hover(&mut rt.s, "row-4", r.x + 4, r.y));
    assert!(rt.s.rows.is_at("", 4));
    rt.click("row-5");
    assert_eq!(rt.s.current_row, 5);
}

/// A menu walks with j/k and the arrows, skips its disabled row, wraps, and Return runs the row
/// (as `qt/tests/tst_navigation.qml` and the Rune gallery drive theirs).
#[test]
fn menu_walks_with_vim_keys_and_arrows() {
    let mut rt = Rt::new("menus");
    rt.click("menu-new");
    assert_eq!(rt.s.ran, Some("New"));
    for (keys, ran) in [
        (&["j", "Enter"][..], "Open…"),
        (&["Down", "Enter"], "Save"),
        (&["k", "Enter"], "Open…"),
        (&["j", "j", "Enter"], "Sidebar"),
        (&["End", "Down", "Enter"], "New"),
        (&["Home", "Up", "Up", "Enter"], "Word wrap"),
    ] {
        rt.keys(keys);
        match ran {
            "Sidebar" => assert!(!rt.s.sidebar_shown, "j skips the disabled row to Sidebar and Return flips it"),
            "Word wrap" => assert!(rt.s.word_wrap, "Up from the first row wraps to the last"),
            _ => assert_eq!(rt.s.ran, Some(ran), "after {keys:?}"),
        }
    }
    rt.click("context-date");
    assert!(!rt.s.sort_by_name);
    rt.keys(&["k", "Enter"]);
    assert!(rt.s.sort_by_name);
}

#[test]
fn tabs_and_rail_choose() {
    let mut rt = Rt::new("navigation");
    rt.click("tab-3");
    assert_eq!(rt.s.tab, 3);
    rt.click("rail-4");
    assert_eq!(rt.s.panel, 4);
    rt.focus("tabs");
    for (keys, want) in [(&["Left"][..], 2), (&["h"], 1), (&["Home"], 0), (&["Left"], 0), (&["End"], 3), (&["l"], 3)] {
        rt.keys(keys);
        assert_eq!(rt.s.tab, want, "tabs after {keys:?}");
    }
    rt.focus("rail");
    for (keys, want) in [(&["Home"][..], 0), (&["j"], 1), (&["Down"], 2), (&["k", "Up"], 0), (&["End"], 4), (&["Down"], 4)] {
        rt.keys(keys);
        assert_eq!(rt.s.panel, want, "rail after {keys:?}");
    }
}

#[test]
fn forms_check_and_choose() {
    let mut rt = Rt::new("forms");
    rt.click("check-0");
    assert!(rt.s.checks[0]);
    assert!(rt.t.find("check-3").is_none(), "a disabled checkbox takes no clicks");
    rt.click("radio-hidden");
    assert_eq!(rt.s.position, "hidden");
    rt.focus("radio");
    rt.keys(&["k"]);
    assert_eq!(rt.s.position, "bottom");
    rt.keys(&["Up", "Up"]);
    assert_eq!(rt.s.position, "top");
    rt.focus("check-1");
    rt.key("Space");
    assert!(!rt.s.checks[1]);
}

/// MultiSelect's keys as the GUI bindings take them: Down into the list, Return toggles, Up off the
/// first row goes back to the search, typing filters, Return in the search toggles the first
/// match, Escape closes and clears.
#[test]
fn multiselect_keys() {
    let mut rt = Rt::new("pickers");
    rt.focus("notify");
    rt.keys(&["Down", "Enter"]);
    assert!(rt.s.notify.contains(&"calendar".to_string()));
    rt.keys(&["j", "Space"]);
    assert!(!rt.s.notify.contains(&"mail".to_string()));
    rt.keys(&["k", "k"]);
    assert_eq!(rt.s.notify_state.row, None);
    rt.keys(&["b", "a", "Enter"]);
    assert!(rt.s.notify.contains(&"battery".to_string()));
    rt.key("Esc");
    assert!(!rt.s.notify_state.open && rt.s.notify_state.search.is_empty());
    rt.click("swatch-5");
    assert_eq!(rt.s.swatch, 5);
    rt.click("discard-cancel");
    assert_eq!(rt.s.discard_selected, 0);
}

#[test]
fn data_selects_and_folds() {
    let mut rt = Rt::new("data");
    rt.click("table-4");
    assert_eq!(rt.s.table_selected, 4);
    rt.focus("table");
    rt.keys(&["k", "k", "Enter"]);
    assert_eq!(rt.s.table_selected, 2);
    rt.click("tree-rune-toggle");
    assert!(rt.s.tree_expanded.contains(&"rune".to_string()));
    rt.focus("tree");
    rt.s.tree_cursor = "qt".into();
    rt.key("Left");
    assert!(!rt.s.tree_expanded.contains(&"qt".to_string()), "Left folds an open branch");
    rt.key("Right");
    assert!(rt.s.tree_expanded.contains(&"qt".to_string()));
    rt.keys(&["j", "Enter"]);
    assert_eq!(rt.s.tree_selected, "qt/button");
}

#[test]
fn layout_picks_and_steps() {
    let mut rt = Rt::new("layout");
    rt.click("side-trash");
    assert_eq!(rt.s.sidebar_selected, "trash");
    rt.click("cal-2026-11-20");
    assert_eq!(rt.s.day_selected, Date::new(2026, 11, 20));
    rt.focus("cal");
    rt.keys(&["j", "j", "Right", "Enter"]);
    assert_eq!(rt.s.day_selected, Date::new(2026, 12, 5), "the month follows the cursor");
    assert_eq!(rt.s.month, (2026, 12));
    rt.click("cal-prev");
    assert_eq!(rt.s.month, (2026, 11));
    rt.focus("handle");
    rt.keys(&["l", "l"]);
    assert_eq!(rt.s.sidebar_width, 27);
}

#[test]
fn editing_selects_and_seeks() {
    let mut rt = Rt::new("timeline");
    rt.click("clip-1-0");
    assert_eq!(rt.s.clip, Some((1, 0)));
    rt.focus("timeline");
    rt.key("Right");
    assert_eq!(rt.s.playhead, 13.0);
    rt.key("Shift+Right");
    assert_eq!(rt.s.playhead, 18.0, "Shift steps by the labeled step (5 s at 90 columns for 30 s)");
    rt.key("End");
    assert_eq!(rt.s.playhead, 30.0);

    let mut rt = Rt::new("color");
    rt.focus("picker");
    let s0 = rt.s.color.s;
    rt.key("Right");
    assert!((rt.s.color.s - s0 - 0.01).abs() < 1e-9);
    rt.click("swatch-0");
    let red = rt.t.shown().palette.named["red"];
    assert_eq!(rt.s.color.rgba().hex()[..7], red.hex()[..7]);
}

/// The search list types into its query, filters, moves and activates; a query with no match
/// leaves nothing under the cursor.
#[test]
fn search_list_filters_moves_and_activates() {
    let mut rt = Rt::new("composed");
    rt.focus("searchlist");
    rt.keys(&["Down", "Enter"]);
    assert_eq!(rt.s.activated, Some(2));
    rt.keys(&[" ", "t"]);
    assert_eq!(rt.s.query, "term t");
    rt.keys(&["Backspace", "Backspace"]);
    rt.keys(&["Esc"]);
    assert_eq!(rt.s.query, "", "Escape clears the query first");
    rt.keys(&["c", "a"]);
    assert_eq!(rt.s.query, "ca");
    rt.key("Enter");
    assert_eq!(rt.s.activated, Some(0), "a new query puts the cursor on the first match");
    rt.keys(&["Backspace", "Backspace"]);
    rt.click("search-row-1");
    assert_eq!(rt.s.activated, Some(1));
}

#[test]
fn spinners_turn_and_pulse_on_the_clock() {
    assert_eq!(Spinner::frame(0), 0);
    assert_eq!(Spinner::frame(95), 1);
    assert_eq!(Spinner::frame(899), 9);
    assert_eq!(Spinner::frame(900), 0);
    assert_eq!(Spinner::opacity(0), 0.5);
    assert!((Spinner::opacity(420) - 1.0).abs() < 1e-9);
}

/// Tab walks a section's controls; `[` and `]` change section.
#[test]
fn window_keys_walk_focus_and_sections() {
    let mut g = Gallery { theme: fixture("tokyo-night"), state: State::init(), section: 0, scroll: 0, body: (20, 7) };
    let key = |g: &mut Gallery, code: KeyCode| {
        g.event(&Event::Key(KeyEvent::new(code, KeyModifiers::NONE)));
    };
    key(&mut g, KeyCode::Tab);
    assert_eq!(g.state.focus, Some("plain"));
    key(&mut g, KeyCode::BackTab);
    key(&mut g, KeyCode::BackTab);
    assert_eq!(g.state.focus, Some("power"));
    key(&mut g, KeyCode::Esc);
    key(&mut g, KeyCode::Char(']'));
    assert_eq!(g.name(), "toggles");
    key(&mut g, KeyCode::Char('['));
    key(&mut g, KeyCode::Char('['));
    assert_eq!(g.name(), "tokens");
    assert_eq!(g.event(&Event::Key(KeyEvent::new(KeyCode::Char('q'), KeyModifiers::NONE))), Flow::Quit);
}

/// The text of each row of a component drawn alone at its size.
fn rows_of(t: &Theme, d: &dyn Draw) -> Vec<String> {
    let (w, h) = d.size(t);
    let area = Rect::new(0, 0, w, h);
    let mut buf = Buffer::empty(area);
    d.draw(t, area, &mut buf);
    (0..h).map(|y| (0..w).map(|x| buf[(x, y)].symbol()).collect::<String>()).collect()
}

/// A Dialog's title, message and body are a blank row apart (the body has no specimen in the
/// gallery); with only a body it starts at the padding.
#[test]
fn dialog_body_sits_a_row_under_the_message() {
    let t = fixture("tokyo-night");
    let d = Dialog::new("Title").message("Message").body(SectionHeader("Body".into())).width(30).scrim(false);
    let rows = rows_of(&t, &d);
    let at = |s: &str| rows.iter().position(|r| r.contains(s)).unwrap_or_else(|| panic!("no {s} in {rows:#?}"));
    assert_eq!((at("Title"), at("Message"), at("Body"), at("Confirm")), (2, 4, 6, 8), "{rows:#?}");
    assert_eq!(rows.len(), 11);
    let d = Dialog::new("").body(SectionHeader("Body".into())).width(30).scrim(false);
    let rows = rows_of(&t, &d);
    let at = |s: &str| rows.iter().position(|r| r.contains(s)).unwrap();
    assert_eq!((at("Body"), at("Confirm"), rows.len()), (2, 4, 7), "{rows:#?}");
}

/// The Scrubber's knob is the Slider's: foreground, never bold, under the cursor too.
#[test]
fn scrubber_knob_is_never_bold() {
    use omarchy_ratatui::ratatui::style::Modifier;
    use omarchy_ratatui::ui::Scrubber;
    let t = fixture("tokyo-night");
    let s = Scrubber::new(42.0, 130.0).width(45).cursor(true);
    let area = Rect::new(0, 0, 45, 1);
    let mut buf = Buffer::empty(area);
    s.draw(&t, area, &mut buf);
    let knob = (0..45).map(|x| &buf[(x, 0)]).find(|c| c.symbol() == "●").expect("a knob");
    assert!(!knob.modifier.contains(Modifier::BOLD));
}

/// The reverse-video cells of a section as drawn now, relative to the keyed part's origin.
fn reversed_cells(rt: &Rt, key: &str) -> Vec<(u16, u16)> {
    use omarchy_ratatui::ratatui::style::Modifier;
    rt.t.clear_hits();
    let buf = draw_page(&rt.s, &rt.t, rt.section);
    let r = rt.find(key);
    let a = buf.area;
    (a.y..a.bottom()).flat_map(|y| (a.x..a.right()).map(move |x| (x, y))).filter(|p| buf[*p].modifier.contains(Modifier::REVERSED)).map(|(x, y)| (x - r.x, y - r.y)).collect()
}

/// A PasswordField is a TextField: focused, it shows the caret after its dots (the lock at column
/// 2, the dots from 4), and only then.
#[test]
fn password_field_shows_its_caret_while_focused() {
    let mut rt = Rt::new("forms");
    assert_eq!(reversed_cells(&rt, "password"), vec![]);
    rt.focus("password");
    assert_eq!(reversed_cells(&rt, "password"), vec![(4 + 8, 0)], "after the eight dots of hunter22");
    rt.key("!");
    assert_eq!(reversed_cells(&rt, "password"), vec![(4 + 9, 0)]);
}

/// A click on a row's action (Office shows it: the action cursor) is the action, not the row.
#[test]
fn list_row_action_click_reaches_the_action() {
    let mut rt = Rt::new("lists");
    let r = rt.find("row-6-action");
    assert_eq!(rt.t.hit_at(r.x, r.y).as_deref(), Some("row-6-action"));
    rt.click("row-6-action");
    assert_eq!(rt.s.ran, Some("forget"));
    assert_eq!(rt.s.current_row, 2, "the row isn't chosen");
    assert_eq!(rt.s.focus, Some("rows"));
    rt.click("row-6");
    assert_eq!(rt.s.current_row, 6, "the rest of the row chooses it");
}

/// Composed: Tab goes to the Wi-Fi panel's networks, then the search list, in reading order (as
/// Qt's gallery walks them: the Card's ScrollArea before the SearchList).
#[test]
fn composed_tab_walks_networks_then_the_search_list() {
    assert_eq!(tab_walk("composed", KeyCode::Tab, 3), [Some("networks"), Some("searchlist"), Some("networks")]);
}

/// Where focus goes on each of `n` presses of `key` in a section's window.
fn tab_walk(section: &str, key: KeyCode, n: usize) -> Vec<Option<&'static str>> {
    let section = SECTIONS.iter().position(|(name, _)| *name == section).unwrap();
    let mut g = Gallery { theme: fixture("tokyo-night"), state: State::init(), section, scroll: 0, body: (20, 7) };
    (0..n)
        .map(|_| {
            g.event(&Event::Key(KeyEvent::new(key, KeyModifiers::NONE)));
            g.state.focus
        })
        .collect()
}

/// In every section Tab walks the focusable controls in order and wraps, and Shift+Tab walks back:
/// no control keeps Tab (a list's sections, a dialog's buttons) in the window.
#[test]
fn tab_walks_every_section_and_wraps() {
    for (name, _) in SECTIONS {
        let f = interact::focusables(name);
        if f.is_empty() {
            continue;
        }
        let want: Vec<_> = f.iter().chain(f.iter()).map(|k| Some(*k)).collect();
        assert_eq!(tab_walk(name, KeyCode::Tab, 2 * f.len()), want, "Tab in {name}");
        let back: Vec<_> = f.iter().rev().chain(f.iter().rev()).map(|k| Some(*k)).collect();
        assert_eq!(tab_walk(name, KeyCode::BackTab, 2 * f.len()), back, "Shift+Tab in {name}");
    }
}

/// A click on a revealable PasswordField's eye shows the text (and focuses the field), again hides it.
#[test]
fn password_eye_click_reveals() {
    let mut rt = Rt::new("forms");
    rt.click("password-eye");
    assert!(rt.s.revealed);
    assert_eq!(rt.s.focus, Some("password"));
    rt.click("password-eye");
    assert!(!rt.s.revealed);
}
