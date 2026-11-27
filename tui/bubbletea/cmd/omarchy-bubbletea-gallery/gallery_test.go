package main

// Behavior tests: the keys and clicks the GUI galleries' tests drive (qt/tests/tst_*.qml, the
// Rune and Ratatui galleries' tests), on the terminal gallery's state; the section sizes
// spec/terminal.md lists; and snapshots in the shared canonical format.

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"testing"

	tea "charm.land/bubbletea/v2"

	o "omarchy-ui/tui/bubbletea/omarchy"
	"omarchy-ui/tui/bubbletea/ui"
)

func repo() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Join(filepath.Dir(file), "..", "..", "..", "..")
}

// fixture resolves a fixture theme through the core, as --snap does under bin/review.
func fixture(tb testing.TB, name, rounding string) *o.Theme {
	tb.Helper()
	tb.Setenv("OMARCHY_UI_THEME_DIR", filepath.Join(repo(), "fixtures/themes", name))
	tb.Setenv("OMARCHY_UI_ROUNDING", rounding)
	tb.Setenv("OMARCHY_UI_REDUCE_MOTION", "1")
	t, err := o.Load()
	if err != nil {
		tb.Fatal(err)
	}
	t.SetDepth(o.TrueColor)
	return t
}

// rt is a section of the gallery with its state, drawn so its keys can be found and clicked.
type rt struct {
	tb      testing.TB
	t       *o.Theme
	s       State
	section string
}

func newRT(tb testing.TB, section string) *rt {
	r := &rt{tb: tb, t: fixture(tb, "tokyo-night", "0"), s: initState(), section: section}
	r.draw()
	return r
}

func (r *rt) draw() {
	r.t.ClearHits()
	drawPage(&r.s, r.t, r.section)
}

func (r *rt) find(key string) o.Rect {
	a, ok := r.t.Find(key)
	if !ok {
		r.tb.Fatalf("no %s drawn in %s", key, r.section)
	}
	return a
}

// click clicks the middle of a keyed part, as a mouse press would.
func (r *rt) click(key string) {
	r.draw()
	a := r.find(key)
	col, row := a.X+a.W/2, a.Y+a.H/2
	hit := r.t.HitAt(col, row)
	if f := focusOf(r.section, hit); f != "" {
		r.s.focus = f
	}
	r.s.click(r.t, r.section, hit, col, a)
	r.draw()
}

func (r *rt) focus(f string) {
	if !slices.Contains(focusables(r.section), f) {
		r.tb.Fatalf("%s is not focusable in %s", f, r.section)
	}
	r.s.focus = f
}

// press is a key as Bubble Tea delivers it.
func press(k string) tea.KeyPressMsg {
	switch k {
	case "Down":
		return tea.KeyPressMsg{Code: tea.KeyDown}
	case "Up":
		return tea.KeyPressMsg{Code: tea.KeyUp}
	case "Left":
		return tea.KeyPressMsg{Code: tea.KeyLeft}
	case "Right":
		return tea.KeyPressMsg{Code: tea.KeyRight}
	case "Shift+Right":
		return tea.KeyPressMsg{Code: tea.KeyRight, Mod: tea.ModShift}
	case "Home":
		return tea.KeyPressMsg{Code: tea.KeyHome}
	case "End":
		return tea.KeyPressMsg{Code: tea.KeyEnd}
	case "Enter":
		return tea.KeyPressMsg{Code: tea.KeyEnter}
	case "Esc":
		return tea.KeyPressMsg{Code: tea.KeyEscape}
	case "Space", " ":
		return tea.KeyPressMsg{Code: tea.KeySpace, Text: " "}
	case "Backspace":
		return tea.KeyPressMsg{Code: tea.KeyBackspace}
	case "Tab":
		return tea.KeyPressMsg{Code: tea.KeyTab}
	case "Shift+Tab":
		return tea.KeyPressMsg{Code: tea.KeyTab, Mod: tea.ModShift}
	}
	return tea.KeyPressMsg{Code: []rune(k)[0], Text: k}
}

func (r *rt) key(k string) bool {
	took := r.s.key(press(k))
	r.draw()
	return took
}

func (r *rt) keys(ks ...string) {
	for _, k := range ks {
		r.key(k)
	}
}

// sizes is the natural size of every section, as spec/terminal.md lists it (96 columns wide).
var sizes = []struct {
	name string
	rows int
}{
	{"buttons", 7}, {"toggles", 11}, {"inputs", 8}, {"slider", 7}, {"surfaces", 34}, {"lists", 25},
	{"tiles", 25}, {"progress", 12}, {"messages", 26}, {"menus", 26}, {"navigation", 15},
	{"forms", 26}, {"pickers", 34}, {"data", 12}, {"layout", 27}, {"timeline", 26}, {"color", 20},
	{"composed", 37}, {"tokens", 18},
}

func fixtureThemes(tb testing.TB) []string {
	entries, err := os.ReadDir(filepath.Join(repo(), "fixtures/themes"))
	if err != nil {
		tb.Fatal(err)
	}
	var out []string
	for _, e := range entries {
		if e.IsDir() {
			out = append(out, e.Name())
		}
	}
	return out
}

// ansi is the shared snapshot tool, when built (bin/build core).
func ansi(tb testing.TB) string {
	p := filepath.Join(repo(), "target/release/omarchy-ansi")
	if _, err := os.Stat(p); err != nil {
		tb.Skip("no target/release/omarchy-ansi (bin/build core)")
	}
	return p
}

// Every section lays out at its spec'd size and writes a canonical snapshot of exactly that size
// in every fixture theme, at both radii; the page background is never painted; an explicit size
// crops or pads.
func TestEverySectionSnapsCanonicallyAtItsSizeInEveryTheme(t *testing.T) {
	tool := ansi(t)
	if len(sections) != len(sizes) {
		t.Fatal("sections and sizes")
	}
	dir := t.TempDir()
	for _, theme := range fixtureThemes(t) {
		for _, rounding := range []string{"0", "8"} {
			th := fixture(t, theme, rounding)
			for i, sz := range sizes {
				if sections[i].name != sz.name {
					t.Fatalf("sections in spec/gallery.md order: %s", sz.name)
				}
				s := initState()
				p := page(&s, sz.name)
				if w, h := p.Size(th); w != cols || h != sz.rows {
					t.Errorf("%s in %s: %dx%d, want %dx%d", sz.name, theme, w, h, cols, sz.rows)
				}
				snap := th.RenderSnapshot(p, cols, 0)
				f := filepath.Join(dir, sz.name+".ans")
				if err := os.WriteFile(f, []byte(snap), 0o644); err != nil {
					t.Fatal(err)
				}
				want := "96x" + strconv.Itoa(sz.rows)
				if out, err := exec.Command(tool, "check", f, "--size", want).CombinedOutput(); err != nil {
					t.Fatalf("%s in %s r%s: %s", sz.name, theme, rounding, out)
				}
				canon, err := exec.Command(tool, "canon", f).Output()
				if err != nil || !bytes.Equal(canon, []byte(snap)) {
					t.Fatalf("%s in %s r%s is not canonical", sz.name, theme, rounding)
				}
				if !strings.Contains(snap, "\x1b[0m ") && !strings.Contains(snap, "\n ") && !strings.HasPrefix(snap, " ") {
					t.Errorf("%s in %s: no default-background cell", sz.name, theme)
				}
			}
		}
	}
	th := fixture(t, "catppuccin-latte", "8")
	s := initState()
	small := th.RenderSnapshot(page(&s, "lists"), 40, 5)
	f := filepath.Join(dir, "small.ans")
	os.WriteFile(f, []byte(small), 0o644)
	if out, err := exec.Command(tool, "check", f, "--size", "40x5").CombinedOutput(); err != nil {
		t.Fatalf("an explicit size crops: %s", out)
	}
}

func TestTogglesToggle(t *testing.T) {
	r := newRT(t, "toggles")
	r.click("switch-0")
	if !r.s.switches[0] {
		t.Error("switch-0")
	}
	r.click("bluetooth")
	if !r.s.bluetooth {
		t.Error("bluetooth")
	}
	r.focus("wifi")
	r.key("Space")
	if r.s.wifi {
		t.Error("space flips")
	}
	r.key("Enter")
	if !r.s.wifi {
		t.Error("enter flips")
	}
}

func TestButtonGroupAndInputsTakeKeys(t *testing.T) {
	r := newRT(t, "buttons")
	r.focus("align")
	r.keys("Right", "l")
	if r.s.align != 2 {
		t.Error("stops at the end")
	}
	r.keys("h", "Home")
	if r.s.align != 0 {
		t.Error("home")
	}
	r.click("align-2")
	if r.s.align != 2 {
		t.Error("click")
	}

	r = newRT(t, "inputs")
	r.focus("search")
	r.keys("w", "i", "f", "i", "Backspace")
	if r.s.search.Value() != "wif" {
		t.Errorf("search %q", r.s.search.Value())
	}
	r.click("gaps-inc")
	if r.s.gaps != 11 {
		t.Error("gaps-inc")
	}
	r.focus("gaps")
	r.keys("Down", "Down")
	if r.s.gaps != 9 {
		t.Error("gaps keys")
	}
	// The dropdown opens on Return, walks with j/k, picks with Return.
	r.focus("theme")
	r.keys("Enter", "j", "j", "k", "Enter")
	if themes[r.s.themeChoice] != "Catppuccin" || r.s.dropdownOpen {
		t.Error("dropdown keys")
	}
	// A click on the trigger opens it, a click on a row picks.
	r.click("theme")
	if !r.s.dropdownOpen {
		t.Error("click opens")
	}
	r.click("theme-list-3")
	if themes[r.s.themeChoice] != "Nord" {
		t.Error("click picks")
	}
}

func TestSlidersStepAndFollowClicks(t *testing.T) {
	r := newRT(t, "slider")
	r.focus("stepped")
	r.key("Right")
	if r.s.stepped != 1 {
		t.Error("right")
	}
	r.keys("Left", "Left")
	if r.s.stepped != 0.5 {
		t.Error("left")
	}
	a := r.find("level")
	r.s.click(r.t, "slider", "level", a.X+1, a)
	if r.s.level != 0 {
		t.Error("click at the start")
	}
}

// The list's cursor moves with the keys, reveals rows past the cap, activates; the pointer moves
// it only when it reaches another cell (CursorModel's gate).
func TestListsCursorFollowsKeysAndPointer(t *testing.T) {
	r := newRT(t, "lists")
	r.focus("rows")
	if !r.s.rows.IsAt("", 1) {
		t.Fatal("starts on 1")
	}
	r.key("j")
	if !r.s.rows.IsAt("", 2) {
		t.Error("j")
	}
	r.key("k")
	if !r.s.rows.IsAt("", 1) {
		t.Error("k")
	}
	r.key("End")
	if !r.s.rows.IsAt("", 8) || r.s.rowsOffset != 2 {
		t.Error("the last row is revealed")
	}
	r.key("Home")
	if r.s.rowsOffset != 0 {
		t.Error("home")
	}
	r.keys("j", "Enter")
	if r.s.currentRow != 1 {
		t.Error("enter")
	}
	// The pointer: the first sample primes, the next in another cell moves.
	a := r.find("row-4")
	if r.s.hover("row-4", a.X+3, a.Y) {
		t.Error("the first sample only primes")
	}
	if !r.s.hover("row-4", a.X+4, a.Y) || !r.s.rows.IsAt("", 4) {
		t.Error("the pointer moves the cursor")
	}
	r.click("row-5")
	if r.s.currentRow != 5 {
		t.Error("click")
	}
}

// A menu walks with j/k and the arrows, skips its disabled row, wraps, and Return runs the row.
func TestMenuWalksWithVimKeysAndArrows(t *testing.T) {
	r := newRT(t, "menus")
	r.click("menu-new")
	if r.s.ran != "New" {
		t.Error("click runs")
	}
	for _, c := range []struct {
		keys []string
		ran  string
	}{
		{[]string{"j", "Enter"}, "Open…"},
		{[]string{"Down", "Enter"}, "Save"},
		{[]string{"k", "Enter"}, "Open…"},
		{[]string{"j", "j", "Enter"}, "Sidebar"},
		{[]string{"End", "Down", "Enter"}, "New"},
		{[]string{"Home", "Up", "Up", "Enter"}, "Word wrap"},
	} {
		r.keys(c.keys...)
		switch c.ran {
		case "Sidebar":
			if r.s.sidebarShown {
				t.Error("j skips the disabled row to Sidebar and Return flips it")
			}
		case "Word wrap":
			if !r.s.wordWrap {
				t.Error("Up from the first row wraps to the last")
			}
		default:
			if r.s.ran != c.ran {
				t.Errorf("after %v: %q", c.keys, r.s.ran)
			}
		}
	}
	r.click("context-date")
	if r.s.sortByName {
		t.Error("context click")
	}
	r.keys("k", "Enter")
	if !r.s.sortByName {
		t.Error("context keys")
	}
}

func TestTabsAndRailChoose(t *testing.T) {
	r := newRT(t, "navigation")
	r.click("tab-3")
	if r.s.tab != 3 {
		t.Error("tab click")
	}
	r.click("rail-4")
	if r.s.panel != 4 {
		t.Error("rail click")
	}
	r.focus("tabs")
	for _, c := range []struct {
		keys []string
		want int
	}{{[]string{"Left"}, 2}, {[]string{"h"}, 1}, {[]string{"Home"}, 0}, {[]string{"Left"}, 0}, {[]string{"End"}, 3}, {[]string{"l"}, 3}} {
		r.keys(c.keys...)
		if r.s.tab != c.want {
			t.Errorf("tabs after %v: %d", c.keys, r.s.tab)
		}
	}
	r.focus("rail")
	for _, c := range []struct {
		keys []string
		want int
	}{{[]string{"Home"}, 0}, {[]string{"j"}, 1}, {[]string{"Down"}, 2}, {[]string{"k", "Up"}, 0}, {[]string{"End"}, 4}, {[]string{"Down"}, 4}} {
		r.keys(c.keys...)
		if r.s.panel != c.want {
			t.Errorf("rail after %v: %d", c.keys, r.s.panel)
		}
	}
}

func TestFormsCheckAndChoose(t *testing.T) {
	r := newRT(t, "forms")
	r.click("check-0")
	if !r.s.checks[0] {
		t.Error("check-0")
	}
	if _, ok := r.t.Find("check-3"); ok {
		t.Error("a disabled checkbox takes no clicks")
	}
	r.click("radio-hidden")
	if r.s.position != "hidden" {
		t.Error("radio click")
	}
	r.focus("radio")
	r.keys("k")
	if r.s.position != "bottom" {
		t.Error("k")
	}
	r.keys("Up", "Up")
	if r.s.position != "top" {
		t.Error("stops at the top")
	}
	r.focus("check-1")
	r.key("Space")
	if r.s.checks[1] {
		t.Error("space")
	}
	r.focus("password")
	r.keys("!", "Backspace", "Backspace")
	if r.s.password.Value() != "hunter2" {
		t.Errorf("password %q", r.s.password.Value())
	}
}

// MultiSelect's keys as the GUI bindings take them.
func TestMultiSelectKeys(t *testing.T) {
	r := newRT(t, "pickers")
	r.focus("notify")
	r.keys("Down", "Enter")
	if !slices.Contains(r.s.notify, "calendar") {
		t.Error("down, enter toggles the first row")
	}
	r.keys("j", "Space")
	if slices.Contains(r.s.notify, "mail") {
		t.Error("j, space toggles the second")
	}
	r.keys("k", "k")
	if r.s.notifyState.Row != ui.None {
		t.Error("up off the first row goes back to the search")
	}
	r.keys("b", "a", "Enter")
	if !slices.Contains(r.s.notify, "battery") {
		t.Error("typing filters, enter toggles the first match")
	}
	r.key("Esc")
	if r.s.notifyState.Open || r.s.notifyState.Search != "" {
		t.Error("esc closes and clears")
	}
	r.click("swatch-5")
	if r.s.swatch != 5 {
		t.Error("swatch click")
	}
	r.click("discard-cancel")
	if r.s.discardSelected != 0 {
		t.Error("discard click")
	}
}

func TestDataSelectsAndFolds(t *testing.T) {
	r := newRT(t, "data")
	r.click("table-4")
	if r.s.tableSelected != 4 {
		t.Error("table click")
	}
	r.focus("table")
	r.keys("k", "k", "Enter")
	if r.s.tableSelected != 2 {
		t.Error("table keys")
	}
	r.click("tree-rune-toggle")
	if !slices.Contains(r.s.treeExpanded, "rune") {
		t.Error("toggle")
	}
	r.focus("tree")
	r.s.treeCursor = "qt"
	r.key("Left")
	if slices.Contains(r.s.treeExpanded, "qt") {
		t.Error("Left folds an open branch")
	}
	r.key("Right")
	if !slices.Contains(r.s.treeExpanded, "qt") {
		t.Error("Right opens")
	}
	r.keys("j", "Enter")
	if r.s.treeSelected != "qt/button" {
		t.Error("enter selects")
	}
}

func TestLayoutPicksAndSteps(t *testing.T) {
	r := newRT(t, "layout")
	r.click("side-trash")
	if r.s.sidebarSelected != "trash" {
		t.Error("sidebar click")
	}
	r.click("cal-2026-11-20")
	if r.s.daySelected != ui.NewDate(2026, 11, 20) {
		t.Error("day click")
	}
	r.focus("cal")
	r.keys("j", "j", "Right", "Enter")
	if r.s.daySelected != ui.NewDate(2026, 12, 5) || r.s.month != [2]int{2026, 12} {
		t.Error("the month follows the cursor")
	}
	r.click("cal-prev")
	if r.s.month != [2]int{2026, 11} {
		t.Error("prev")
	}
	r.focus("handle")
	r.keys("l", "l")
	if r.s.sidebarWidth != 27 {
		t.Error("handle")
	}
}

func TestEditingSelectsAndSeeks(t *testing.T) {
	r := newRT(t, "timeline")
	r.click("clip-1-0")
	if r.s.clip == nil || *r.s.clip != (ui.ClipRef{Track: 1, Clip: 0}) {
		t.Error("clip click")
	}
	r.focus("timeline")
	r.key("Right")
	if r.s.playhead != 13 {
		t.Error("right")
	}
	r.key("Shift+Right")
	if r.s.playhead != 18 {
		t.Error("Shift steps by the labeled step (5 s at 90 columns for 30 s)")
	}
	r.key("End")
	if r.s.playhead != 30 {
		t.Error("end")
	}

	r = newRT(t, "color")
	r.focus("picker")
	s0 := r.s.color.S
	r.key("Right")
	if d := r.s.color.S - s0 - 0.01; d > 1e-9 || d < -1e-9 {
		t.Error("right")
	}
	r.click("swatch-0")
	red := r.t.Shown().Palette.Named["red"]
	if r.s.color.Rgba().Hex()[:7] != red.Hex()[:7] {
		t.Error("swatch picks")
	}
}

// The search list types into its query, filters, moves and activates.
func TestSearchListFiltersMovesAndActivates(t *testing.T) {
	r := newRT(t, "composed")
	r.focus("searchlist")
	r.keys("Down", "Enter")
	if r.s.activated != 2 {
		t.Errorf("activated %d", r.s.activated)
	}
	r.keys(" ", "t")
	if r.s.query != "term t" {
		t.Errorf("query %q", r.s.query)
	}
	r.keys("Backspace", "Backspace", "Esc")
	if r.s.query != "" {
		t.Error("Escape clears the query first")
	}
	r.keys("c", "a")
	if r.s.query != "ca" {
		t.Error("typing")
	}
	r.key("Enter")
	if r.s.activated != 0 {
		t.Error("a new query puts the cursor on the first match")
	}
	r.keys("Backspace", "Backspace")
	r.click("search-row-1")
	if r.s.activated != 1 {
		t.Error("click")
	}
}

func TestSpinnersTurnAndPulseOnTheClock(t *testing.T) {
	if ui.SpinnerFrame(0) != 0 || ui.SpinnerFrame(95) != 1 || ui.SpinnerFrame(899) != 9 || ui.SpinnerFrame(900) != 0 {
		t.Error("frames")
	}
	if ui.SpinnerOpacity(0) != 0.5 || ui.SpinnerOpacity(420) < 1-1e-9 {
		t.Error("pulse")
	}
}

// Tab walks a section's controls; [ and ] change section; q quits; clicks land through the
// window's frame.
func TestWindowKeysWalkFocusAndSections(t *testing.T) {
	th := fixture(t, "tokyo-night", "0")
	s := initState()
	var m tea.Model = gallery{theme: th, state: &s, width: 100, height: 40}
	send := func(msg tea.Msg) tea.Cmd {
		var cmd tea.Cmd
		m, cmd = m.Update(msg)
		return cmd
	}
	g := func() gallery { return m.(gallery) }
	send(press("Tab"))
	if s.focus != "plain" {
		t.Errorf("focus %q", s.focus)
	}
	send(press("Shift+Tab"))
	send(press("Shift+Tab"))
	if s.focus != "power" {
		t.Errorf("back %q", s.focus)
	}
	send(press("Esc"))
	send(press("]"))
	if g().name() != "toggles" {
		t.Error("]")
	}
	send(press("["))
	send(press("["))
	if g().name() != "tokens" {
		t.Error("[ wraps")
	}
	send(press("]"))
	send(press("]"))
	// A click through the window: the first switch, under the header.
	th.ClearHits()
	m.View()
	a, ok := th.Find("switch-0")
	if !ok {
		t.Fatal("switch-0 not drawn in the window")
	}
	send(tea.MouseClickMsg{X: a.X + 1, Y: a.Y, Button: tea.MouseLeft})
	if !s.switches[0] {
		t.Error("click through the window")
	}
	cmd := send(press("q"))
	if cmd == nil {
		t.Fatal("q quits")
	}
	if _, ok := cmd().(tea.QuitMsg); !ok {
		t.Error("q quits")
	}
}

// rowsOf is the text of each row of a component drawn alone at its size.
func rowsOf(t *o.Theme, c o.Component) []string {
	w, h := c.Size(t)
	buf := o.NewBuffer(o.R(0, 0, w, h))
	c.Draw(t, buf.Area, buf)
	rows := make([]string, h)
	for y := range h {
		var b strings.Builder
		for x := range w {
			b.WriteString(buf.At(x, y).Symbol)
		}
		rows[y] = b.String()
	}
	return rows
}

// A Dialog's title, message and body are a blank row apart (the body has no specimen in the
// gallery); with only a body it starts at the padding.
func TestDialogBodySitsARowUnderTheMessage(t *testing.T) {
	th := fixture(t, "tokyo-night", "0")
	check := func(d ui.Dialog, want map[string]int, h int) {
		t.Helper()
		rows := rowsOf(th, d)
		if len(rows) != h {
			t.Errorf("%d rows, want %d:\n%s", len(rows), h, strings.Join(rows, "\n"))
		}
		for s, y := range want {
			got := slices.IndexFunc(rows, func(r string) bool { return strings.Contains(r, s) })
			if got != y {
				t.Errorf("%s on row %d, want %d:\n%s", s, got, y, strings.Join(rows, "\n"))
			}
		}
	}
	check(ui.NewDialog("Title").Message("Message").Body(ui.SectionHeader("Body")).Width(30).Scrim(false),
		map[string]int{"Title": 2, "Message": 4, "Body": 6, "Confirm": 8}, 11)
	check(ui.NewDialog("").Body(ui.SectionHeader("Body")).Width(30).Scrim(false),
		map[string]int{"Body": 2, "Confirm": 4}, 7)
}

// The Scrubber's knob is the Slider's: foreground, never bold, under the cursor too.
func TestScrubberKnobIsNeverBold(t *testing.T) {
	th := fixture(t, "tokyo-night", "0")
	buf := o.NewBuffer(o.R(0, 0, 45, 1))
	ui.NewScrubber(42, 130).Width(45).Cursor(true).Draw(th, buf.Area, buf)
	i := slices.IndexFunc(buf.Cells, func(c o.Cell) bool { return c.Symbol == "●" })
	if i < 0 || buf.Cells[i].Attrs&o.Bold != 0 {
		t.Errorf("knob at %d is bold or missing", i)
	}
}

// reversedCells are the reverse-video cells of the section as drawn now, relative to the keyed
// part's origin.
func (r *rt) reversedCells(key string) [][2]int {
	r.t.ClearHits()
	buf := drawPage(&r.s, r.t, r.section)
	a := r.find(key)
	var out [][2]int
	for i, c := range buf.Cells {
		if c.Attrs&o.Reverse != 0 {
			out = append(out, [2]int{buf.Area.X + i%buf.Area.W - a.X, buf.Area.Y + i/buf.Area.W - a.Y})
		}
	}
	return out
}

// A PasswordField is a TextField: focused, it shows the caret after its dots (the lock at column
// 2, the dots from 4), and only then.
func TestPasswordFieldShowsItsCaretWhileFocused(t *testing.T) {
	r := newRT(t, "forms")
	if got := r.reversedCells("password"); len(got) != 0 {
		t.Errorf("unfocused: %v", got)
	}
	r.focus("password")
	if got := r.reversedCells("password"); !slices.Equal(got, [][2]int{{4 + 8, 0}}) {
		t.Errorf("focused: %v, want after the eight dots of hunter22", got)
	}
	r.key("!")
	if got := r.reversedCells("password"); !slices.Equal(got, [][2]int{{4 + 9, 0}}) {
		t.Errorf("after typing: %v", got)
	}
}

// A click on a row's action (Office shows it: the action cursor) is the action, not the row.
func TestListRowActionClickReachesTheAction(t *testing.T) {
	r := newRT(t, "lists")
	a := r.find("row-6-action")
	if got := r.t.HitAt(a.X, a.Y); got != "row-6-action" {
		t.Fatalf("the action cell hits %q", got)
	}
	r.click("row-6-action")
	if r.s.ran != "forget" || r.s.currentRow != 2 || r.s.focus != "rows" {
		t.Errorf("ran %q, current row %d, focus %q", r.s.ran, r.s.currentRow, r.s.focus)
	}
	r.click("row-6")
	if r.s.currentRow != 6 {
		t.Errorf("the rest of the row chooses it: %d", r.s.currentRow)
	}
}

// tabWalk is where focus goes on each of n presses of k in a section's window.
func tabWalk(t *testing.T, section, k string, n int) []string {
	s := initState()
	var m tea.Model = gallery{theme: fixture(t, "tokyo-night", "0"), state: &s, section: sectionIndex(section), width: 100, height: 40}
	var seen []string
	for range n {
		m, _ = m.Update(press(k))
		seen = append(seen, s.focus)
	}
	return seen
}

// Composed: Tab goes to the Wi-Fi panel's networks, then the search list, in reading order (as
// Qt's gallery walks them: the Card's ScrollArea before the SearchList).
func TestComposedTabWalksNetworksThenTheSearchList(t *testing.T) {
	if got := tabWalk(t, "composed", "Tab", 3); !slices.Equal(got, []string{"networks", "searchlist", "networks"}) {
		t.Errorf("Tab walks %v", got)
	}
}

// In every section Tab walks the focusable controls in order and wraps, and Shift+Tab walks back:
// no control keeps Tab (a list's sections, a dialog's buttons) in the window.
func TestTabWalksEverySectionAndWraps(t *testing.T) {
	for _, sec := range sections {
		f := focusables(sec.name)
		if len(f) == 0 {
			continue
		}
		if got, want := tabWalk(t, sec.name, "Tab", 2*len(f)), slices.Concat(f, f); !slices.Equal(got, want) {
			t.Errorf("Tab in %s: %v, want %v", sec.name, got, want)
		}
		r := slices.Clone(f)
		slices.Reverse(r)
		if got, want := tabWalk(t, sec.name, "Shift+Tab", 2*len(f)), slices.Concat(r, r); !slices.Equal(got, want) {
			t.Errorf("Shift+Tab in %s: %v, want %v", sec.name, got, want)
		}
	}
}

// A click on a revealable PasswordField's eye shows the text (and focuses the field), again hides
// it.
func TestPasswordEyeClickReveals(t *testing.T) {
	r := newRT(t, "forms")
	r.click("password-eye")
	if !r.s.revealed || r.s.focus != "password" {
		t.Errorf("revealed %v, focus %q", r.s.revealed, r.s.focus)
	}
	r.click("password-eye")
	if r.s.revealed {
		t.Error("the eye hides it again")
	}
}
