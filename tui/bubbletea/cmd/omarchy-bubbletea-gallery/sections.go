package main

import (
	"strconv"

	o "omarchy-ui/tui/bubbletea/omarchy"
	"omarchy-ui/tui/bubbletea/ui"
)

// Every gallery section's specimens, in spec/gallery.md order, at the cell sizes spec/terminal.md
// lists. A section is the page frame (2 columns, 1 row of padding, the title, a blank row) around
// its body.

type section struct{ name, title string }

var sections = []section{
	{"buttons", "Buttons"},
	{"toggles", "Toggles"},
	{"inputs", "Inputs"},
	{"slider", "Slider"},
	{"surfaces", "Surfaces"},
	{"lists", "Lists"},
	{"tiles", "Tiles"},
	{"progress", "Progress"},
	{"messages", "Messages"},
	{"menus", "Menus"},
	{"navigation", "Navigation"},
	{"forms", "Forms"},
	{"pickers", "Pickers"},
	{"data", "Data"},
	{"layout", "Layout"},
	{"timeline", "Timeline"},
	{"color", "Color"},
	{"composed", "Composed"},
	{"tokens", "Tokens"},
}

// Every section is this wide; the body gets 4 fewer.
const (
	cols    = 96
	content = cols - 4
)

// g is a glyph by codepoint.
func g(cp rune) string { return string(cp) }

// page is the page frame: padding 2 × 1, the section title (a SectionHeader), a blank row, the
// body.
func page(s *State, name string) o.Col {
	title := name
	for _, sec := range sections {
		if sec.name == name {
			title = sec.title
		}
	}
	return o.Column(0,
		o.Blank{W: cols, H: 1},
		o.Line(0, o.Blank{W: 2, H: 1}, o.Column(1, ui.SectionHeader(title), body(s, name))),
		o.Blank{W: cols, H: 1},
	)
}

func body(s *State, name string) o.Component {
	switch name {
	case "buttons":
		return buttons(s)
	case "toggles":
		return toggles(s)
	case "inputs":
		return inputs(s)
	case "slider":
		return slider(s)
	case "surfaces":
		return surfaces(s)
	case "lists":
		return lists(s)
	case "tiles":
		return tiles(s)
	case "progress":
		return progress()
	case "messages":
		return messages(s)
	case "menus":
		return menus(s)
	case "navigation":
		return navigation(s)
	case "forms":
		return forms(s)
	case "pickers":
		return pickers(s)
	case "data":
		return data(s)
	case "layout":
		return layout(s)
	case "timeline":
		return timeline(s)
	case "color":
		return color(s)
	case "composed":
		return composed(s)
	}
	return tokens()
}

func (s *State) hot(key string) bool { return s.focus == key }

// caret is the caret index for a focused text field, else None.
func (s *State) caret(key string, pos int) int { return ui.If(s.hot(key), pos) }

func buttons(s *State) o.Component {
	return o.Column(1,
		o.Line(1,
			ui.NewButton("Plain").Cursor(s.hot("plain")).Key("plain"),
			ui.NewButton("Bordered").Bordered(true).Cursor(s.hot("bordered")).Key("bordered"),
			ui.NewButton("Hover").Bordered(true).Cursor(true),
			ui.NewButton("Selected").Bordered(true).Selected(true),
			ui.NewButton("Icon").Icon(g(0xF013)).Bordered(true).Cursor(s.hot("icon")).Key("icon"),
			ui.NewIconButton(g(0xF011)).Bordered(true).Cursor(s.hot("power")).Key("power"),
			ui.NewIconButton(g(0xF021)).Bordered(true).Cursor(true),
		),
		ui.NewButtonGroup([]string{"Left", "Center", "Right"}, s.align).Cursor(ui.If(s.hot("align"), s.align)).Key("align"),
	)
}

func toggles(s *State) o.Component {
	switches := o.Line(2)
	for i, on := range s.switches {
		k := "switch-" + strconv.Itoa(i)
		switches = switches.Child(ui.NewToggleSwitch(on).Cursor(i >= 2 || s.hot(k)).Key(k))
	}
	return o.Column(1,
		switches,
		ui.NewToggle("Wi-Fi").Description("Connected to Basecamp").On(s.wifi).Width(45).Cursor(s.hot("wifi")).Key("wifi"),
		ui.NewToggle("Bluetooth").Description("Off").On(s.bluetooth).Width(45).Cursor(true).Key("bluetooth"),
	)
}

func inputs(s *State) o.Component {
	return o.Column(1,
		o.Line(1,
			ui.NewTextField(s.search.Value()).Placeholder("Search…").Width(28).Cursor(s.hot("search")).Caret(s.caret("search", s.search.Position())).Key("search"),
			ui.NewTextField(s.name.Value()).Width(28).Cursor(s.hot("name")).Caret(s.caret("name", s.name.Position())).Key("name"),
			ui.NewTextField("Hovered").Width(28).Cursor(true),
		),
		o.Line(2,
			ui.NewNumberField(s.gaps).Label("Gaps").Range(0, 40).Width(15).Cursor(s.hot("gaps")).Key("gaps"),
			ui.NewDropdown(themes[s.themeChoice]).Label("Theme").Width(30).Cursor(s.hot("theme")).Key("theme"),
		),
	)
}

func slider(s *State) o.Component {
	return o.Column(1,
		ui.NewSlider(s.level).Width(45).Cursor(s.hot("level")).Key("level"),
		ui.NewSlider(s.stepped).Notches(5).Width(45).Cursor(s.hot("stepped")).Key("stepped"),
	)
}

func surfaces(s *State) o.Component {
	card := ui.NewCard().Title("Network").Width(38).Child(
		ui.NewToggle("Wi-Fi").Description("Basecamp").On(s.networkOn).Width(32).Cursor(s.hot("network")).Key("network"),
		ui.Separator(32),
		o.Column(0, ui.NewMenuRow("Basecamp").Checked(true).Width(32), ui.NewMenuRow("Basecamp Guest").Highlighted(true).Width(32), ui.NewMenuRow("Coffee Shop").Width(32)),
	)
	right := o.Column(1,
		ui.NewTooltip("Restart Wi-Fi"),
		o.FillBox(43, 11, ui.NewConfirmDialog("Remove the Coffee Shop network?").Width(39).Selected(s.confirmSelected).Key("confirm")),
	)
	layers := ui.NewPane().Title("Layers").Subtitle("3 items").Divider(ui.Left).
		Action(ui.NewIconButton(g(0xF067)).Cursor(s.hot("add-layer")).Key("add-layer")).Width(30).
		Child(o.Column(0, ui.NewMenuRow("Background").Width(25), ui.NewMenuRow("Title").Checked(true).Width(25), ui.NewMenuRow("Logo").Width(25)))
	night := ui.NewPane().Title("Night light").Subtitle("Sunset to sunrise").Bordered(true).
		Action(ui.NewToggleSwitch(s.nightLight).Cursor(s.hot("night-light")).Key("night-light")).Width(30).
		Child(ui.NewSettingRow("Strength").Control(ui.NewSlider(0.6).Width(13)).Width(24))
	headings := o.Line(2,
		o.Column(1,
			ui.NewHeading("Settings").Subtitle("Omarchy").Level(ui.LevelDisplay).Width(30),
			ui.NewHeading("Appearance").Subtitle("Theme, font and rounding").Level(ui.LevelHeading).Width(30),
		),
		o.Column(1,
			ui.NewHeading("Frame").Trailing(ui.NewIconButton(g(0xF141))).Width(30),
			ui.NewHeading("notes.md").Subtitle("~/Documents").Centered(true).Width(30),
		),
	)
	return o.Column(1, o.Line(2, card, right), o.Line(2, layers, night), headings)
}

func lists(s *State) o.Component {
	const w = 53
	lock := g(0xF033E)
	cur := func(i int) bool { return s.rows.IsAt("", i) }
	key := func(i int) string { return "row-" + strconv.Itoa(i) }
	rows := []o.Component{
		ui.NewListRow("Coffee Shop").Glyph(g(0xF0928)).Subtitle("WPA2").Value("5 GHz").TrailingGlyph(lock, ui.Neutral).Cursor(cur(0)).Current(s.currentRow == 0).Width(w).Key(key(0)),
		ui.NewListRow("Basecamp Guest").Glyph(g(0xF0925)).Subtitle("Open").Cursor(cur(1)).Current(s.currentRow == 1).Width(w).Key(key(1)),
		ui.NewListRow("Basecamp").Glyph(g(0xF0928)).Status("Connected", ui.Success).Cursor(cur(2)).Current(s.currentRow == 2).Width(w).Key(key(2)),
		ui.NewListRow("Neighbor 5G").Glyph(g(0xF091F)).Subtitle("Out of range").Disabled(true).Width(w).Key(key(3)),
		ui.NewListRow("Messages").Leading(ui.NewAvatar("Messages").SizeAs(ui.AvatarSmall)).Subtitle("From Basecamp").
			Trailing(ui.NewBadge("3").Tone(ui.Accent).Filled(true)).Cursor(cur(4)).Current(s.currentRow == 4).Width(w).Key(key(4)),
		ui.NewListRow("Headphones").Glyph(g(0xF00AF)).Subtitle("Battery 80%").Trailing(ui.NewToggleSwitch(true).Bare()).Cursor(cur(5)).Current(s.currentRow == 5).Width(w).Key(key(5)),
		ui.NewListRow("Office").Glyph(g(0xF0922)).Subtitle("Saved").TrailingGlyph(lock, ui.Neutral).Action(g(0xF0159), true).ActionCursor(true).Cursor(cur(6)).Current(s.currentRow == 6).Width(w).Key(key(6)),
		ui.NewListRow("Library").Glyph(g(0xF0922)).Subtitle("WPA2").TrailingGlyph(lock, ui.Neutral).Action(g(0xF0159), true).Cursor(cur(7)).Current(s.currentRow == 7).Width(w).Key(key(7)),
		ui.NewListRow("Airport Free WiFi").Glyph(g(0xF091F)).Subtitle("Open").Cursor(cur(8)).Current(s.currentRow == 8).Width(w).Key(key(8)),
	}
	area := ui.NewScrollArea(w).MaxRows(7).Offset(s.rowsOffset).Child(rows...)
	chevron := g(0xF0142)
	stays := o.Column(0,
		ui.NewListRow("Appearance").Glyph(g(0xF03D8)).Subtitle("Theme, font, rounding").TrailingGlyph(chevron, ui.Neutral).Width(w),
		ui.NewListRow("Display").Glyph(g(0xF0379)).Subtitle("2560 × 1440").TrailingGlyph(chevron, ui.Neutral).Width(w),
		ui.NewListRow("Headphones").Glyph(g(0xF02CB)).Subtitle("Output").Current(true).TrailingGlyph(g(0xF012C), ui.Accent).Width(w),
	)
	return o.Column(1,
		area,
		stays,
		ui.NewHero(g(0xF0928), "Wi-Fi").Status("Connected to Basecamp").Trailing(ui.NewToggleSwitch(true)).Width(w),
		ui.NewHero(g(0xF00B2), "Bluetooth").Status("No adapter found").Error(true).GlyphOpacity(0.5).Width(w),
		ui.NewKeyValue().Columns(2).
			Pair("Ping", "18 ms").
			PairToned("Packet loss", "4%", ui.Urgent).
			Pair("Receiving", "1.2 MB/s").
			Pair("Sending", "240 KB/s").
			Pair("IP address", "192.168.1.24").
			Pair("Gateway", "192.168.1.1"),
	)
}

func tiles(s *State) o.Component {
	pic := s.picture
	badges := o.Line(1,
		ui.NewBadge("New"),
		ui.NewBadge("3").Tone(ui.Accent).Filled(true),
		ui.NewBadge("12").Tone(ui.Urgent).Filled(true),
		ui.NewBadge("Online").Tone(ui.Success),
		ui.NewBadge("Low").Tone(ui.Warning),
		ui.NewBadge("Offline").Tone(ui.Urgent),
	)
	dots := o.Line(1, ui.StatusDot(ui.Success), ui.StatusDot(ui.Warning), ui.StatusDot(ui.Urgent), ui.StatusDot(ui.Muted), ui.StatusDot(ui.Accent))
	docked := o.Line(2,
		dots,
		ui.NewAvatar("Ryan").Round(true).Status(ui.Success),
		ui.NewAvatar("Picture").Image(pic).Status(ui.Warning),
		ui.NewAvatar("").Glyph(g(0xF048B)).Status(ui.Urgent),
	)
	avatars := o.Line(2,
		ui.NewAvatar("ada").SizeAs(ui.AvatarSmall),
		ui.NewAvatar("Basecamp"),
		ui.NewAvatar("Omarchy").SizeAs(ui.AvatarLarge).Round(true),
		ui.NewAvatar("").Glyph(g(0xF06A9)),
		ui.NewAvatar("Picture").Image(pic).SizeAs(ui.AvatarSmall),
		ui.NewAvatar("Picture").Image(pic).Round(true),
		ui.NewAvatar("Picture").Image(pic).SizeAs(ui.AvatarLarge),
		ui.NewAvatar("Missing").Image(nil),
	)
	byName := o.Line(2,
		ui.NewAvatar("Files").Icon("org.gnome.Nautilus"),
		ui.NewAvatar("Terminal").Icon("utilities-terminal"),
		ui.NewAvatar("Firefox").Icon("firefox"),
		ui.NewAvatar("Chromium").Icon("chromium").SizeAs(ui.AvatarSmall),
		ui.NewAvatar("Steam").Icon("steam").SizeAs(ui.AvatarLarge),
		ui.NewAvatar("Btop").Icon("btop").SizeAs(ui.AvatarSmall),
		ui.NewAvatar("Nothing").Icon("no-such-app").Glyph(g(0xF120)),
	)
	grid := ui.NewTileGrid(
		ui.NewTile("Terminal").Glyph(g(0xF120)),
		ui.NewTile("Files").Glyph(g(0xF07B)),
		ui.NewTile("Browser").Glyph(g(0xF0AC)),
		ui.NewTile("Music").Glyph(g(0xF001)),
		ui.NewTile("Settings").Glyph(g(0xF013)),
		ui.NewTile("Calendar").Glyph(g(0xF073)),
		ui.NewTile("Screen Recorder").Glyph(g(0xF03D)),
		ui.NewTile("Photos").Avatar(ui.NewAvatar("Photos").Image(pic)),
	).Columns(4).Cursor(s.tileCursor).Key("tile")
	return o.Column(1, badges, docked, avatars, byName, grid)
}

func progress() o.Component {
	return o.Column(1,
		o.Line(2,
			ui.NewMeter(0.2).Markers(0.5, 0.8).Width(28),
			ui.NewMeter(0.65).Markers(0.5, 0.8).Width(28),
			ui.NewMeter(0.92).Markers(0.5, 0.8).Alarming(true).Width(28),
		),
		ui.NewMeter(0.65).Label("Battery").ValueText("65%").Width(45),
		o.Line(2, ui.NewProgressBar(0.4).Width(43), ui.NewIndeterminateProgressBar().Width(43)),
		o.Line(2, ui.NewSpinner(), ui.NewSpinner(), ui.NewSpinner().Pulse(true), ui.NewSpinner().Pulse(true)),
	)
}

func messages(s *State) o.Component {
	boxes := o.Line(2,
		o.Centered(44, 9, ui.NewEmptyState("No networks found").Glyph(g(0xF05AA)).Hint("Turn on Wi-Fi to scan").Action("Scan").Cursor(s.hot("scan")).Width(44)),
		o.Centered(44, 9, ui.NewEmptyState("Clipboard is empty").Glyph(g(0xF014C)).Hint("Copy something to see it here").Overlay(true).Width(44)),
	)
	return o.Column(1,
		boxes,
		o.Column(1,
			ui.NewBanner("Omarchy 3.2 is available").Tone(ui.Info).Action("Update").Dismissible(true).Width(content),
			ui.NewBanner("Backup finished").Tone(ui.Success).Dismissible(true).Width(content),
			ui.NewBanner("Battery is low (12%)").Tone(ui.Warning).Dismissible(true).Width(content),
			ui.NewBanner("Could not reach the update server").Tone(ui.Error).Dismissible(true).Width(content),
		),
		ui.NewToast("Export finished").Text("omarchy-demo.mp4 · 42 MB").Glyph(g(0xF012C)).Action("Open").Countdown(0.6).Width(45),
	)
}

var menuItems = []string{"New", "Open…", "Save", "Duplicate", "Sidebar", "Word wrap", "Export"}
var menuKeys = []string{"menu-new", "menu-open", "menu-save", "menu-duplicate", "menu-sidebar", "menu-wrap", "menu-export"}
var contextItems = []string{"Cut", "Copy", "Paste", "By name", "By date", "Delete"}
var contextKeys = []string{"context-cut", "context-copy", "context-paste", "context-name", "context-date", "context-delete"}

func menus(s *State) o.Component {
	mc := func(i int) bool { return s.menuCursor == i }
	menu := ui.NewMenu().
		Section("File").
		Item(ui.NewMenuItem("New").Icon(g(0xF15B)).Shortcut("Ctrl+N").Cursor(mc(0)).Key("menu-new")).
		Item(ui.NewMenuItem("Open…").Icon(g(0xF07B)).Shortcut("Ctrl+O").Cursor(mc(1)).Key("menu-open")).
		Item(ui.NewMenuItem("Save").Icon(g(0xF0C7)).Shortcut("Ctrl+S").Cursor(mc(2)).Key("menu-save")).
		Item(ui.NewMenuItem("Duplicate").Icon(g(0xF0C5)).Disabled(true).Key("menu-duplicate")).
		Separator().
		Section("View").
		Item(ui.NewMenuItem("Sidebar").Check(s.sidebarShown).Cursor(mc(4)).Key("menu-sidebar")).
		Item(ui.NewMenuItem("Word wrap").Check(s.wordWrap).Cursor(mc(5)).Key("menu-wrap")).
		Separator().
		Item(ui.NewMenuItem("Export").Icon(g(0xF064)).Submenu(true).Cursor(mc(6)).Key("menu-export"))
	cc := func(i int) bool { return s.hot("context") && s.contextCursor == i }
	context := ui.NewMenu().
		Item(ui.NewMenuItem("Cut").Shortcut("Ctrl+X").Cursor(cc(0)).Key("context-cut")).
		Item(ui.NewMenuItem("Copy").Shortcut("Ctrl+C").Cursor(cc(1)).Key("context-copy")).
		Item(ui.NewMenuItem("Paste").Shortcut("Ctrl+V").Cursor(cc(2)).Key("context-paste")).
		Separator().
		Item(ui.NewMenuItem("By name").Radio(s.sortByName).Cursor(cc(3)).Key("context-name")).
		Item(ui.NewMenuItem("By date").Radio(!s.sortByName).Cursor(cc(4)).Key("context-date")).
		Separator().
		Item(ui.NewMenuItem("Delete").Shortcut("Del").Destructive(true).Cursor(cc(5)).Key("context-delete"))
	caps := o.Line(1)
	for _, k := range []string{"Super", "Ctrl", "Alt", "Shift", "Tab", "Esc", "Enter", "Backspace", "Up", "Space", "K"} {
		caps = caps.Child(ui.Keycap(k))
	}
	sheet := ui.NewShortcutSheet().
		Group("File", ui.Shortcut{Combo: "Ctrl+N", Description: "New file"}, ui.Shortcut{Combo: "Ctrl+O", Description: "Open"}, ui.Shortcut{Combo: "Ctrl+S", Description: "Save"}).
		Group("View", ui.Shortcut{Combo: "Ctrl+B", Description: "Sidebar"}, ui.Shortcut{Combo: "Ctrl+Shift+P", Description: "Commands"}, ui.Shortcut{Combo: "F11", Description: "Full screen"}).
		Columns(2)
	return o.Column(1, o.Line(2, menu, context, ui.NewTooltip("Save").Shortcut("Ctrl+S")), caps, ui.KeyCombo("Ctrl+Shift+P"), sheet)
}

var rail = []ui.RailItem{{Glyph: g(0xF07B), Label: "Files"}, {Glyph: g(0xF002), Label: "Search"}, {Glyph: g(0xF126), Label: "Source control"}, {Glyph: g(0xF188), Label: "Debug"}, {Glyph: g(0xF013), Label: "Settings"}}

func navigation(s *State) o.Component {
	ib := func(cp rune, cursor bool) ui.IconButton { return ui.NewIconButton(g(cp)).Cursor(cursor) }
	bar := ui.NewToolbar(content).
		Leading(ib(0xF032, false), ib(0xF033, true), ib(0xF0CD, false), ui.ToolbarSeparator{}, ib(0xF0CA, false), ib(0xF0CB, false), ui.ToolbarSeparator{}, ib(0xF03E, false), ib(0xF0C1, false)).
		Center(ui.NewHeading("notes.md").Centered(true)).
		Trailing(ui.NewButton("Share").Bordered(true)).
		Overflow(true)
	tabs := ui.NewTabs([]string{"General", "Display", "Audio", "Network"}, s.tab).Cursor(s.tabCursor).Width(content)
	r := ui.NewIconRail(rail, s.panel).Cursor(ui.If(s.hot("rail"), s.panel))
	name := rail[s.panel].Label
	panel := o.Custom(40, 5, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		o.TextCenter(t, buf, area.X, area.Y+(area.H-1)/2, area.W, name, t.Dim())
	})
	return o.Column(1, bar, tabs, o.Line(2, r, panel))
}

func forms(s *State) o.Component {
	settings := o.Column(1,
		ui.NewSettingRow("Night light").Description("Warm the screen after sunset").Control(ui.NewToggleSwitch(s.nightToggle).Cursor(s.hot("night")).Key("night")).Width(60),
		ui.NewSettingRow("Theme").Description("Applies to every app").Control(ui.NewDropdown(themes[s.themeChoice]).Width(23)).Width(60),
		ui.NewSettingRow("Gaps").Description("Space between windows").Control(ui.NewNumberField(s.gaps).Range(0, 40).Width(15)).Width(60),
	)
	labels := []string{"Show seconds", "Show date", "Show weekday", "Show week number", "Twelve-hour clock"}
	checks := o.Column(0)
	for i, l := range labels {
		k := "check-" + strconv.Itoa(i)
		c := ui.NewCheckbox(l).Checked(s.checks[i]).Cursor(i == 2 || s.hot(k)).Disabled(i == 3).Key(k)
		if i == 4 {
			c = c.Description("Show AM and PM")
		}
		checks = checks.Child(c)
	}
	positions := []string{"top", "bottom", "hidden"}
	pos := 0
	for i, p := range positions {
		if p == s.position {
			pos = i
		}
	}
	radios := ui.NewRadioGroup([]ui.Option{{Value: "top", Label: "Top"}, {Value: "bottom", Label: "Bottom"}, {Value: "hidden", Label: "Hidden"}}, s.position).Cursor(ui.If(s.hot("radio"), pos)).Key("radio")
	passwords := o.Line(1,
		ui.NewPasswordField("").Placeholder("Password").Width(28),
		ui.NewPasswordField(s.password.Value()).Revealable(true).Revealed(s.revealed).Cursor(s.hot("password")).Caret(s.caret("password", s.password.Position())).Width(28).Key("password"),
		ui.NewPasswordField("letmein").Error("Wrong password, try again").Width(28),
	)
	return o.Column(1,
		settings,
		o.Line(4, checks, radios),
		passwords,
		ui.NewTextArea("Back up your dotfiles\nbefore switching themes,\nthen run omarchy update.").Width(60),
	)
}

func pickers(s *State) o.Component {
	bar := ui.NewMultiSelect("modules", modules, s.modules).Label("Bar modules").Cursor(s.hot("modules")).Width(33)
	notify := ui.NewMultiSelect("notify", notifyOptions, s.notify).
		Label("Notify for").
		Open(s.notifyState.Open).
		Search(s.notifyState.Search).
		Highlighted(s.notifyState.Row).
		Cursor(s.hot("notify")).
		Width(33)
	// The wells' colors come from the theme at draw time, so they fade: draw them in a Custom.
	wells := o.Custom(23, 1, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		ui.NewColorWell(t.Accent()).Draw(t, o.R(area.X, area.Y, 7, 1), buf)
		ui.NewColorWell(t.Success()).Cursor(true).Draw(t, o.R(area.X+8, area.Y, 7, 1), buf)
		ui.NewColorWell(t.Urgent()).Draw(t, o.R(area.X+16, area.Y, 7, 1), buf)
	})
	swatch, swatchCursor := s.swatch, s.swatchCursor
	strip := o.Custom(23, 2, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		colors := []o.Rgba{t.Accent(), t.Urgent(), t.Warning(), t.Success(), t.Fg(), t.Dim(), t.Faint(), t.Shown().Palette.Muted}
		ui.NewSwatchStrip(colors, swatch).Cursor(swatchCursor).Key("swatch").Draw(t, area, buf)
	})
	card := ui.NewCard().Title("Displays").Width(53).Child(o.Column(0,
		ui.NewMenuRow("Built-in display").Checked(true).Width(47),
		ui.NewMenuRow("External monitor").Width(47),
		ui.NewMenuRow("Projector").Width(47),
	))
	dialog := ui.NewDialog("Discard changes?").Message("Your edits to Tokyo Night will be lost.").Labels("Keep editing", "Discard").
		Destructive(true).Selected(s.discardSelected).Width(46).Key("discard")
	modal := o.Custom(53, 13, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		card.Draw(t, area, buf)
		dialog.Draw(t, area, buf)
	})
	return o.Column(1, o.Line(2, bar, o.Sized(33, 11, notify)), wells, strip, modal)
}

func treeNodes() []ui.TreeNode {
	file := g(0xF0219)
	return []ui.TreeNode{ui.Node("root", "omarchy-ui", g(0xF0770),
		ui.Node("qt", "qt", g(0xF0770), ui.Node("qt/button", "Button.qml", file), ui.Node("qt/table", "Table.qml", file)),
		ui.Node("rune", "rune", g(0xF024B), ui.Node("rune/lib", "lib.rs", file)),
		ui.Node("spec", "spec", g(0xF0770), ui.Node("spec/tokens", "tokens.md", file)),
		ui.Node("readme", "README.md", file),
	)}
}

var tableRows = [][]string{
	{"assets", "Folder", "—", "2026-11-02"},
	{"Cargo.toml", "TOML", "1.2 KB", "2026-11-07"},
	{"README.md", "Markdown", "4.8 KB", "2026-11-05"},
	{"screenshot-of-the-new-settings-panel.png", "PNG image", "1.4 MB", "2026-11-08"},
	{"src", "Folder", "—", "2026-11-08"},
	{"theme.toml", "TOML", "812 B", "2026-10-30"},
}

func data(s *State) o.Component {
	table := ui.NewTable(ui.NewTableColumn("Name"), ui.NewTableColumn("Kind").Width(11), ui.NewTableColumn("Size").Width(9).Right(), ui.NewTableColumn("Modified").Width(13)).
		Rows(tableRows).Sort(0, true).Selected(s.tableSelected).Cursor(s.tableCursor).Width(60).Key("table")
	tree := ui.NewTree(treeNodes()...).Expanded(s.treeExpanded...).Selected(s.treeSelected).Cursor(s.treeCursor).Width(28).Key("tree")
	return o.Column(0, o.Line(2, table, tree))
}

func sidebarSections() []ui.SidebarSection {
	return []ui.SidebarSection{
		{Title: "Library", Items: []ui.SidebarItem{{ID: "all", Label: "All files", Glyph: g(0xF024B)}, {ID: "recent", Label: "Recent", Glyph: g(0xF02DA)}, {ID: "starred", Label: "Starred", Glyph: g(0xF04CE)}, {ID: "trash", Label: "Trash", Glyph: g(0xF0A7A)}}},
		{Title: "Places", Items: []ui.SidebarItem{{ID: "documents", Label: "Documents", Glyph: g(0xF0219)}, {ID: "downloads", Label: "Downloads", Glyph: g(0xF01DA)}}},
	}
}

func layout(s *State) o.Component {
	cursor := ""
	if s.hot("sidebar") {
		cursor = s.sidebarSelected
	}
	side := ui.NewSidebar(sidebarSections()...).Selected(s.sidebarSelected).Cursor(cursor).Width(s.sidebarWidth).Key("side")
	empty := o.Custom(10, 1, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		o.TextCenter(t, buf, area.X, area.Y+max(0, area.H-1)/2, area.W, "No file selected", t.Faint())
	})
	split := ui.NewSplitView(88, 12).Pane(ui.NewSplitPane(side).SizeCells(s.sidebarWidth).Min(15), ui.NewSplitPane(empty)).Cursor(ui.If(s.hot("handle"), 0))
	framed := o.Custom(90, 14, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		inner := o.Frame(t, buf, area, t.Track(), "", o.Ink{})
		split.Draw(t, inner, buf)
	})
	return o.Column(1, framed, ui.NewCalendar(s.month[0], s.month[1]).Today(ui.NewDate(2026, 11, 8)).Selected(s.daySelected).Cursor(s.dayCursor).Key("cal"))
}

var dragRows = []string{"Workspaces", "Clock", "Battery", "Network"}

func timeline(s *State) o.Component {
	tl := ui.NewTimeline(30).
		Position(s.playhead).
		Track(ui.Clip{Start: 0, End: 7.5, Label: "Intro"}, ui.Clip{Start: 8, End: 18, Label: "Demo"}, ui.Clip{Start: 18.5, End: 30, Label: "Outro"}).
		Track(ui.Clip{Start: 2, End: 12, Label: "Voiceover"}, ui.Clip{Start: 14, End: 26, Label: "Music"}).
		Selected(s.clip).
		Cursor(s.hot("timeline")).
		SizeCells(90, 10)
	drag := s.dragCursor
	list := o.Custom(45, 5, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		y := area.Y
		for i, l := range dragRows {
			if i == 3 {
				ui.DropIndicator(area.W).Draw(t, o.R(area.X, y, area.W, 1), buf)
				y++
			}
			r := o.R(area.X, y, area.W, 1)
			if i == drag {
				o.Fill(t, buf, r, t.Shown().Controls.Hover.Fill)
			}
			ui.NewDragHandle().Cursor(i == drag).Draw(t, o.R(area.X+1, y, 1, 1), buf)
			o.TextFit(t, buf, area.X+3, y, l, area.W-4, t.Fg())
			t.Hit("drag-"+strconv.Itoa(i), r)
			y++
		}
	})
	crop := o.Custom(30, 9, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		o.Fill(t, buf, area, t.Shown().Controls.Normal.Fill)
		ui.NewCropFrame(area.W, area.H).Draw(t, area, buf)
	})
	return o.Column(1, tl, ui.NewScrubber(s.scrub, 130).Width(45).Cursor(s.hot("scrubber")), o.Line(2, list, crop))
}

func color(s *State) o.Component {
	return o.Column(0, ui.NewColorPicker(s.color).Cursor(s.hot("picker")).SizeCells(38, 16))
}

func searchItems() []ui.SearchItem {
	return []ui.SearchItem{
		{Title: "Terminal", Glyph: g(0xF120), Subtitle: "App", Detail: "Open a new terminal window."},
		{Title: "Terminal font", Glyph: g(0xF031), Subtitle: "Style", Detail: "Pick the monospace font every terminal uses."},
		{Title: "Terminal theme", Glyph: g(0xF1FC), Subtitle: "Style", Detail: "Match the terminal colors to the desktop theme."},
		{Title: "Intermediate CA", Glyph: g(0xF0A3), Subtitle: "Certificate", Detail: "Trust a certificate authority for this machine."},
	}
}

func matching(query string) []ui.SearchItem {
	var out []ui.SearchItem
	for _, it := range searchItems() {
		if ui.SearchMatches(it.Title, query) {
			out = append(out, it)
		}
	}
	return out
}

func composed(s *State) o.Component {
	const w = 39
	lock := g(0xF033E)
	wc := func(i int) bool { return s.wifiRows.IsAt("", i) }
	expansion := o.Line(1, ui.NewPasswordField("").Placeholder("Password").Width(23), ui.NewButton("Connect").Bordered(true))
	networks := ui.NewScrollArea(w).MaxRows(18).Child(
		ui.NewListRow("Basecamp").Glyph(g(0xF0928)).Status("Connected", ui.Success).Current(true).Cursor(wc(0)).Width(w).Key("wifi-0"),
		ui.NewListRow("Basecamp Guest").Glyph(g(0xF0925)).Subtitle("Open").Action(g(0xF0159), true).Cursor(wc(1)).Width(w).Key("wifi-1"),
		ui.NewListRow("Coffee Shop").Glyph(g(0xF0922)).Subtitle("WPA2").TrailingGlyph(lock, ui.Neutral).Expansion(expansion).Cursor(wc(2)).Width(w).Key("wifi-2"),
		ui.NewListRow("Library").Glyph(g(0xF091F)).Subtitle("Saved").TrailingGlyph(lock, ui.Neutral).Cursor(wc(3)).Width(w).Key("wifi-3"),
	)
	panel := ui.NewCard().Width(45).Child(
		ui.NewHero(g(0xF0928), "Wi-Fi").Status("Connected to Basecamp").Trailing(ui.NewToggleSwitch(true)).Width(w),
		ui.SectionHeader("Networks"),
		networks,
		ui.Separator(w),
		ui.NewKeyValue().Pair("IP", "192.168.1.24").Pair("Gateway", "192.168.1.1").Pair("DNS", "1.1.1.1"),
	)
	const iw = 40
	inspector := ui.NewPane().
		Title("Recording 1").
		Subtitle("1920 × 1080 · 0:42").
		Divider(ui.Left).
		Action(ui.NewIconButton(g(0xF0E2)), ui.NewIconButton(g(0xF01E)), ui.NewButton("Export").Bordered(true)).
		Width(45).
		Child(
			ui.NewTabs([]string{"Shape", "Cursor", "Zoom"}, s.inspectorTab).Width(iw),
			o.Column(0,
				ui.SectionHeader("Frame"),
				ui.NewSettingRow("Radius").Control(ui.NewSlider(0.4).Width(14)).Width(iw),
				ui.NewSettingRow("Border").Control(ui.NewDropdown("Solid").Width(14)).Width(iw),
				ui.NewSettingRow("Shadow").Control(ui.NewToggleSwitch(true)).Cursor(true).Width(iw),
			),
			o.Column(0,
				ui.SectionHeader("Background"),
				ui.NewSettingRow("Fill").Control(o.Custom(7, 1, func(t *o.Theme, area o.Rect, buf *o.Buffer) { ui.NewColorWell(t.Accent()).Draw(t, area, buf) })).Width(iw),
				ui.NewSettingRow("Padding").Control(ui.NewNumberField(24).Range(0, 96).Width(15)).Width(iw),
			),
		)
	cursor := ui.None
	if s.searchCursor.Active {
		cursor = s.searchCursor.Index()
	}
	search := ui.NewSearchList("search", s.query).Items(matching(s.query)...).Cursor(cursor).Detail(true).SizeCells(45, 12)
	empty := ui.NewSearchList("search-empty", "zzz").Items(matching("zzz")...).SizeCells(38, 9)
	return o.Column(1, o.Line(2, panel, inspector), o.Line(2, search, empty))
}

func tokens() o.Component {
	names := []string{"foreground", "background", "accent", "urgent", "muted"}
	swatches := o.Custom(content, 3, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		p := &t.Shown().Palette
		colors := []o.Rgba{p.Foreground, p.Background, p.Accent, p.Urgent, p.Muted}
		border := t.Shown().Controls.Normal.Border.Color
		for i, n := range names {
			x := area.X + i*13
			// The swatch is its color's background, outlined in the normal border at its outer
			// columns, so the background swatch shows too.
			o.Fill(t, buf, o.R(x, area.Y, 8, 2), colors[i])
			for dy := range 2 {
				o.Edges(t, buf, x, area.Y+dy, 8, &border)
			}
			o.Text(t, buf, x, area.Y+2, n, 12, t.Fg())
		}
	})
	chips := o.Custom(content, 1, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		c := &t.Shown().Controls
		states := []struct {
			name  string
			state o.State
			ink   o.Rgba
			bold  bool
		}{
			{"normal", c.Normal, t.Fg(), false},
			{"hover-cursor", c.Hover, t.Fg(), false},
			{"focus", c.Focus, t.Fg(), false},
			{"selected", c.Selected, c.Selected.Text, true},
		}
		for i, st := range states {
			x := area.X + i*16
			o.Fill(t, buf, o.R(x, area.Y, 15, 1), st.state.Fill)
			if !st.state.Border.None() {
				b := st.state.Border.Color
				o.Edges(t, buf, x, area.Y, 15, &b)
			}
			o.TextCenter(t, buf, x+1, area.Y, 13, st.name, o.InkOf(st.ink).WithBold(st.bold))
		}
	})
	scale := o.Custom(content, 8, func(t *o.Theme, area o.Rect, buf *o.Buffer) {
		f := &t.Shown().Font
		rows := []struct {
			name string
			px   float64
			bold bool
			ink  o.Rgba
		}{
			{"displayLarge", f.DisplayLarge, true, t.Fg()},
			{"display", f.Display, true, t.Fg()},
			{"heading", f.Heading, true, t.Fg()},
			{"title", f.Title, true, t.Fg()},
			{"subtitle", f.Subtitle, false, t.Fg()},
			{"body", f.Body, false, t.Fg()},
			{"bodySmall", f.BodySmall, false, t.Fg()},
			{"caption", f.Caption, false, t.Dim()},
		}
		for i, r := range rows {
			y := area.Y + i
			o.Text(t, buf, area.X, y, r.name+" "+strconv.FormatFloat(r.px, 'f', -1, 64), 16, t.Dim())
			o.Text(t, buf, area.X+16, y, "The quick brown fox jumps over the lazy dog", 60, o.InkOf(r.ink).WithBold(r.bold))
		}
	})
	return o.Column(1, swatches, chips, scale)
}
