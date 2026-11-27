package ui

import (
	tea "charm.land/bubbletea/v2"

	"omarchy-ui/tui/bubbletea/omarchy"
)

// Surfaces (spec/terminal.md, Surfaces): boxes with a light border in the surface's border color,
// corners by the radius, padding 2 columns × 1 row; and the in-window Pane, Heading,
// SectionHeader, Separator, MenuRow, Tooltip and the dialogs.

func stackSize(t *Theme, kids []Component, gap int) (int, int) {
	w, h := 0, 0
	for i, k := range kids {
		cw, ch := k.Size(t)
		w = max(w, cw)
		h += ch
		if i > 0 {
			h += gap
		}
	}
	return w, h
}

func stackDraw(t *Theme, kids []Component, gap int, area Rect, buf *Buffer) {
	y := area.Y
	for _, k := range kids {
		cw, ch := k.Size(t)
		r := rect(area.X, y, min(cw, area.W), ch).Intersect(area)
		if r.H > 0 && r.W > 0 {
			k.Draw(t, r, buf)
		}
		y += ch + gap
	}
}

func appendKids(kids []Component, more ...Component) []Component {
	return append(append([]Component(nil), kids...), more...)
}

// SectionHeader is the caption that labels a group of rows: bold, textDim.
type SectionHeader string

func (s SectionHeader) Size(*Theme) (int, int) { return width(string(s)), 1 }
func (s SectionHeader) Draw(t *Theme, area Rect, buf *Buffer) {
	omarchy.TextFit(t, buf, area.X, area.Y, string(s), area.W, bold(t.Dim(), true))
}

// Separator is a rule of `─` in track, this many columns wide.
type Separator int

func (s Separator) Size(*Theme) (int, int) { return int(s), 1 }
func (s Separator) Draw(t *Theme, area Rect, buf *Buffer) {
	omarchy.HLine(t, buf, area.X, area.Y, area.W, "─", t.Track())
}

// Card is the raised surface panels, popups and flyouts are drawn on: a box in the popups border
// color, its title a SectionHeader in the top edge, children one row apart inside a 2-column,
// 1-row padding.
type Card struct {
	title    string
	width    int
	children []Component
}

func NewCard() Card { return Card{width: 45} }

func (c Card) Title(s string) Card       { c.title = s; return c }
func (c Card) Width(w int) Card          { c.width = w; return c }
func (c Card) Child(k ...Component) Card { c.children = appendKids(c.children, k...); return c }

func (c Card) Size(t *Theme) (int, int) {
	_, h := stackSize(t, c.children, 1)
	return c.width, h + 4
}

func (c Card) Draw(t *Theme, area Rect, buf *Buffer) {
	omarchy.Clear(buf, area)
	s := &t.Shown().Surfaces.Popups
	inner := omarchy.Frame(t, buf, area, s.Border.Color, c.title, bold(t.Dim(), true))
	stackDraw(t, c.children, 1, inner.Inset(2, 1), buf)
}

// Side is which side a Pane's rule is on.
type Side int

const (
	NoSide Side = iota
	Left
	Top
	Right
	Bottom
)

// Pane is a flat surface inside an app window: padding, an optional Heading with actions,
// children in a column one row apart. Plain (nothing drawn), divided (a track rule on one side)
// or bordered (the controls' normal glass in a box at the radius).
type Pane struct {
	title, subtitle   string
	actions, children []Component
	divider           Side
	bordered          bool
	width             int
}

func NewPane() Pane { return Pane{width: 30} }

func (p Pane) Title(s string) Pane        { p.title = s; return p }
func (p Pane) Subtitle(s string) Pane     { p.subtitle = s; return p }
func (p Pane) Action(k ...Component) Pane { p.actions = appendKids(p.actions, k...); return p }
func (p Pane) Divider(s Side) Pane        { p.divider = s; return p }
func (p Pane) Bordered(on bool) Pane      { p.bordered = on; return p }
func (p Pane) Width(w int) Pane           { p.width = w; return p }
func (p Pane) Child(k ...Component) Pane  { p.children = appendKids(p.children, k...); return p }

// edge is the extra cells on (left, top, right, bottom) for the border or the rule.
func (p Pane) edge() (int, int, int, int) {
	if p.bordered {
		return 1, 1, 1, 1
	}
	switch p.divider {
	case Left:
		return 1, 0, 0, 0
	case Top:
		return 0, 1, 0, 0
	case Right:
		return 0, 0, 1, 0
	case Bottom:
		return 0, 0, 0, 1
	}
	return 0, 0, 0, 0
}

func (p Pane) head(w int) (head, bool) {
	return head{text: p.title, subtitle: p.subtitle, trailing: p.actions, width: w}, p.title != ""
}

func (p Pane) Size(t *Theme) (int, int) {
	l, tp, r, b := p.edge()
	hh := 0
	if h, ok := p.head(sub(p.width, l+r+4)); ok {
		_, hh = h.size(t)
		hh++
	}
	_, ch := stackSize(t, p.children, 1)
	return p.width, tp + b + 2 + hh + ch
}

func (p Pane) Draw(t *Theme, area Rect, buf *Buffer) {
	c := &t.Shown().Controls
	l, tp, r, b := p.edge()
	if p.bordered {
		omarchy.Fill(t, buf, area.Inset(1, 1), c.Normal.Fill)
		omarchy.Frame(t, buf, area, c.Normal.Border.Color, "", Ink{})
	} else {
		switch p.divider {
		case Left:
			omarchy.VLine(t, buf, area.X, area.Y, area.H, "│", t.Track())
		case Right:
			omarchy.VLine(t, buf, area.Right()-1, area.Y, area.H, "│", t.Track())
		case Top:
			omarchy.HLine(t, buf, area.X, area.Y, area.W, "─", t.Track())
		case Bottom:
			omarchy.HLine(t, buf, area.X, area.Bottom()-1, area.W, "─", t.Track())
		}
	}
	inner := Rect{X: area.X + l + 2, Y: area.Y + tp + 1, W: sub(area.W, l+r+4), H: sub(area.H, tp+b+2)}
	y := inner.Y
	if h, ok := p.head(inner.W); ok {
		_, hh := h.size(t)
		h.draw(t, Rect{X: inner.X, Y: y, W: inner.W, H: hh}, buf)
		y += hh + 1
	}
	stackDraw(t, p.children, 1, Rect{X: inner.X, Y: y, W: inner.W, H: sub(inner.Bottom(), y)}, buf)
}

// HeadingLevel is which font token a Heading stands for. The terminal has one size: every level
// is a bold title line with a textDim subtitle under it.
type HeadingLevel int

const (
	LevelTitle HeadingLevel = iota
	LevelHeading
	LevelDisplay
)

// Heading is the title of a pane, toolbar, section or page: a bold line, an optional textDim
// subtitle under it, an optional trailing slot (one column apart) at the end of its width.
type Heading struct {
	text, subtitle string
	level          HeadingLevel
	centered       bool
	width          int
	trailing       []Component
}

func NewHeading(text string) Heading { return Heading{text: text} }

func (h Heading) Subtitle(s string) Heading    { h.subtitle = s; return h }
func (h Heading) Level(l HeadingLevel) Heading { h.level = l; return h }
func (h Heading) Centered(on bool) Heading     { h.centered = on; return h }
func (h Heading) Trailing(k ...Component) Heading {
	h.trailing = appendKids(h.trailing, k...)
	return h
}

// Width fixes the width (the trailing slot sits at its end); by default the widest line plus the
// trailing items.
func (h Heading) Width(w int) Heading { h.width = w; return h }

func (h Heading) head() head {
	return head{text: h.text, subtitle: h.subtitle, centered: h.centered, trailing: h.trailing, width: h.width}
}

func (h Heading) Size(t *Theme) (int, int)              { return h.head().size(t) }
func (h Heading) Draw(t *Theme, area Rect, buf *Buffer) { h.head().draw(t, area, buf) }

type head struct {
	text, subtitle string
	centered       bool
	trailing       []Component
	width          int // 0: natural
}

func (h head) trailingW(t *Theme) int {
	n := len(h.trailing)
	w := 0
	for _, d := range h.trailing {
		dw, _ := d.Size(t)
		w += dw
	}
	w += sub(n, 1)
	if n > 0 {
		w++
	}
	return w
}

func (h head) size(t *Theme) (int, int) {
	tw := h.trailingW(t)
	th := 0
	for _, d := range h.trailing {
		_, dh := d.Size(t)
		th = max(th, dh)
	}
	hh := 2
	if h.subtitle == "" {
		hh = 1
	}
	w := h.width
	if w == 0 {
		if th > 1 || h.centered {
			w = max(width(h.text), width(h.subtitle)) + tw
		} else {
			w = max(width(h.text)+tw, width(h.subtitle))
		}
	}
	return w, max(hh, th)
}

func (h head) draw(t *Theme, area Rect, buf *Buffer) {
	tw := h.trailingW(t)
	linesH := 2
	if h.subtitle == "" {
		linesH = 1
	}
	titleW := sub(area.W, tw)
	trailOnSub := false
	for _, d := range h.trailing {
		_, dh := d.Size(t)
		dy := (max(linesH, dh) - dh) / 2
		if dy+dh > 1 {
			trailOnSub = true
			break
		}
	}
	subW := area.W
	if h.centered || trailOnSub {
		subW = titleW
	}
	line := func(y, w int, s string, k Ink) {
		if h.centered {
			omarchy.TextCenter(t, buf, area.X, y, w, s, k)
		} else {
			omarchy.TextFit(t, buf, area.X, y, s, w, k)
		}
	}
	line(area.Y, titleW, h.text, bold(t.Fg(), true))
	if h.subtitle != "" {
		line(area.Y+1, subW, h.subtitle, ink(t.Dim()))
	}
	x := sub(area.Right(), tw)
	if tw > 0 {
		x++
	}
	for _, d := range h.trailing {
		w, dh := d.Size(t)
		dy := (max(linesH, dh) - dh) / 2
		d.Draw(t, rect(x, area.Y+dy, w, dh).Intersect(area), buf)
		x += w + 1
	}
}

// MenuRow is a list or menu row: the label, an accent check at the end when checked, the hover
// fill when highlighted.
type MenuRow struct {
	label                string
	checked, highlighted bool
	width                int
	key                  string
}

func NewMenuRow(label string) MenuRow { return MenuRow{label: label, width: 30} }

func (m MenuRow) Checked(on bool) MenuRow     { m.checked = on; return m }
func (m MenuRow) Highlighted(on bool) MenuRow { m.highlighted = on; return m }
func (m MenuRow) Width(w int) MenuRow         { m.width = w; return m }
func (m MenuRow) Key(k string) MenuRow        { m.key = k; return m }

func (m MenuRow) Size(*Theme) (int, int) { return m.width, 1 }

func (m MenuRow) Draw(t *Theme, area Rect, buf *Buffer) {
	if m.highlighted {
		omarchy.Fill(t, buf, area, t.Shown().Controls.Hover.Fill)
	}
	omarchy.TextFit(t, buf, area.X+1, area.Y, m.label, sub(area.W, 4), t.Fg())
	if m.checked {
		omarchy.Put(t, buf, sub(area.Right(), 2), area.Y, GlyphCheck, bold(t.Accent(), true))
	}
	if m.key != "" {
		t.Hit(m.key, area)
	}
}

// Tooltip is the tooltip surface: a box in the tooltip border color holding the text and, with a
// shortcut, a KeyCombo one column after it. Three rows.
type Tooltip struct{ text, shortcut string }

func NewTooltip(text string) Tooltip { return Tooltip{text: text} }

func (tt Tooltip) Shortcut(s string) Tooltip { tt.shortcut = s; return tt }

func (tt Tooltip) Size(t *Theme) (int, int) {
	combo := 0
	if tt.shortcut != "" {
		kw, _ := KeyCombo(tt.shortcut).Size(t)
		combo = 1 + kw
	}
	return width(tt.text) + combo + 4, 3
}

func (tt Tooltip) Draw(t *Theme, area Rect, buf *Buffer) {
	omarchy.Clear(buf, area)
	s := &t.Shown().Surfaces.Tooltip
	inner := omarchy.Frame(t, buf, area, s.Border.Color, "", Ink{})
	w := omarchy.TextFit(t, buf, inner.X+1, inner.Y, tt.text, sub(inner.W, 2), s.Text)
	if tt.shortcut != "" {
		kc := KeyCombo(tt.shortcut)
		kw, _ := kc.Size(t)
		kc.Draw(t, rect(inner.X+2+w, inner.Y, kw, 1).Intersect(inner), buf)
	}
}

// Dialog is a modal card on the scrim: title (bold), message, body (those present a blank row
// apart), then a blank row and the buttons at the bottom right (secondary, primary), the selected
// one in the menu's selected look (urgent when destructive). Buttons are keyed `<key>-cancel` and `<key>-accept`.
type Dialog struct {
	title, message, secondary, primary string
	body                               Component
	destructive, noScrim               bool
	selected, width                    int
	key                                string
}

func NewDialog(title string) Dialog { return Dialog{title: title, selected: 1, width: 46} }

// NewConfirmDialog is a Dialog with no title, the message and a destructive Confirm, selected.
func NewConfirmDialog(message string) Dialog {
	return NewDialog("").Message(message).Destructive(true).Selected(1)
}

func (d Dialog) Message(m string) Dialog    { d.message = m; return d }
func (d Dialog) Body(c Component) Dialog    { d.body = c; return d }
func (d Dialog) Destructive(on bool) Dialog { d.destructive = on; return d }
func (d Dialog) Width(w int) Dialog         { d.width = w; return d }
func (d Dialog) Key(k string) Dialog        { d.key = k; return d }

// Labels are the button labels; empty takes the catalog's Cancel / Confirm.
func (d Dialog) Labels(secondary, primary string) Dialog {
	d.secondary, d.primary = secondary, primary
	return d
}

// Selected is the selected button: 0 the secondary, 1 the primary.
func (d Dialog) Selected(i int) Dialog { d.selected = i; return d }

// Scrim draws the scrim over the whole area given (default on); the card centers in it.
func (d Dialog) Scrim(on bool) Dialog { d.noScrim = !on; return d }

// HandleDialogKey: Left/Right/h/l/Tab move the selection. True when handled. (Escape and Return
// are the app's: cancel, and the selected button.)
func HandleDialogKey(selected *int, msg tea.KeyPressMsg) bool {
	switch keyOf(msg) {
	case "left", "h":
		*selected = 0
	case "right", "l":
		*selected = 1
	case "tab", "shift+tab":
		*selected = 1 - min(*selected, 1)
	default:
		return false
	}
	return true
}

func (d Dialog) cardSize(t *Theme) (int, int) {
	inner := sub(d.width, 6)
	h := 0
	if d.title != "" {
		h++
	}
	if d.message != "" {
		if h > 0 {
			h++
		}
		h += len(omarchy.WrapText(d.message, inner))
	}
	if d.body != nil {
		_, bh := d.body.Size(t)
		if h > 0 {
			h++
		}
		h += bh
	}
	// A blank row, the buttons, the padding and the border.
	return d.width, h + 2 + 4
}

func dialogButtonW(label string) int { return max(width(label)+4, 11) }

func dialogButton(t *Theme, buf *Buffer, x, y int, label string, selected, destructive bool) {
	s := t.Shown()
	var f, border, k Rgba
	switch {
	case selected && destructive:
		f, border, k = t.Urgent().WithAlpha(0.22), t.Urgent(), t.Urgent()
	case selected:
		f, border, k = s.Surfaces.Menu.SelectedBackground, s.Surfaces.Menu.SelectedText, s.Surfaces.Menu.SelectedText
	case destructive:
		f, border, k = omarchy.Transparent, t.Urgent().WithAlpha(0.56), t.Fg()
	default:
		f, border, k = omarchy.Transparent, t.Fg().WithAlpha(0.38), t.Fg()
	}
	w := dialogButtonW(label)
	control(t, buf, x, y, w, Look{Fill: f, Border: &border, Text: k})
	omarchy.TextCenter(t, buf, x+1, y, w-2, label, bold(k, selected))
}

func (d Dialog) Size(t *Theme) (int, int) { return d.cardSize(t) }

func (d Dialog) Draw(t *Theme, area Rect, buf *Buffer) {
	s := t.Shown()
	if !d.noScrim {
		omarchy.Scrim(t, buf, area, s.Roles.Scrim)
	}
	cw, ch := d.cardSize(t)
	cw, ch = min(cw, area.W), min(ch, area.H)
	card := rect(area.X+(area.W-cw)/2, area.Y+(area.H-ch)/2, cw, ch)
	omarchy.Clear(buf, card)
	inner := omarchy.Frame(t, buf, card, s.Surfaces.Menu.SelectedText, "", Ink{}).Inset(2, 1)
	y := inner.Y
	if d.title != "" {
		omarchy.TextFit(t, buf, inner.X, y, d.title, inner.W, bold(t.Fg(), true))
		y++
	}
	if d.message != "" {
		if y > inner.Y {
			y++
		}
		for _, line := range omarchy.WrapText(d.message, inner.W) {
			omarchy.TextFit(t, buf, inner.X, y, line, inner.W, t.Fg())
			y++
		}
	}
	if d.body != nil {
		bw, bh := d.body.Size(t)
		if y > inner.Y {
			y++
		}
		d.body.Draw(t, rect(inner.X, y, min(bw, inner.W), bh), buf)
	}
	secondary, primary := d.secondary, d.primary
	if secondary == "" {
		secondary = t.String("common.cancel", "Cancel")
	}
	if primary == "" {
		primary = t.String("common.confirm", "Confirm")
	}
	by := sub(inner.Bottom(), 1)
	sw, pw := dialogButtonW(secondary), dialogButtonW(primary)
	px := sub(inner.Right(), pw)
	sx := sub(px, sw+1)
	dialogButton(t, buf, sx, by, secondary, d.selected == 0, false)
	dialogButton(t, buf, px, by, primary, d.selected == 1, d.destructive)
	if d.key != "" {
		t.Hit(d.key+"-cancel", rect(sx, by, sw, 1))
		t.Hit(d.key+"-accept", rect(px, by, pw, 1))
	}
}
