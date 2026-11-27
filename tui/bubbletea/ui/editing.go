package ui

import (
	"fmt"
	"math"
	"strconv"

	tea "charm.land/bubbletea/v2"

	"omarchy-ui/tui/bubbletea/omarchy"
)

// The pieces media and image editors need (spec/terminal.md, Editing). Handles and the playhead
// are accent; clips, lanes and swatches foreground glass.

// ClockLabel is m:ss (h:mm:ss from an hour; tenths below a 1 s step).
func ClockLabel(secs float64, tenths bool) string {
	if tenths {
		t := int64(math.Round(secs * 10))
		return fmt.Sprintf("%d:%02d.%d", t/600, (t/10)%60, t%10)
	}
	s := int64(math.Round(secs))
	if s >= 3600 {
		return fmt.Sprintf("%d:%02d:%02d", s/3600, (s/60)%60, s%60)
	}
	return fmt.Sprintf("%d:%02d", s/60, s%60)
}

// ---------------------------------------------------------------------------------------------
// Timeline

// Clip is a span on a track.
type Clip struct {
	Start, End float64
	Label      string
}

// RulerStep is the labeled step: the first of 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30 s, 1, 2, 5,
// 10, 15, 30 min, 1 h that keeps major ticks at least 11 columns apart.
func RulerStep(duration float64, cols int) float64 {
	perSec := float64(cols) / math.Max(duration, 1e-9)
	for _, s := range []float64{0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600} {
		if s*perSec >= 11 {
			return s
		}
	}
	return 3600
}

// ClipRef is a clip by track and index.
type ClipRef struct{ Track, Clip int }

// Timeline is a ruler over clip tracks with a playhead (spec/terminal.md, Editing). Clips are
// keyed `clip-<track>-<clip>`, the ruler `timeline-ruler`.
type Timeline struct {
	duration, position float64
	tracks             [][]Clip
	selected           *ClipRef
	cursor             bool
	width, height      int
}

func NewTimeline(duration float64) Timeline {
	return Timeline{duration: math.Max(duration, 0.001), width: 90, height: 10}
}

func (l Timeline) Position(t float64) Timeline { l.position = t; return l }
func (l Timeline) Track(clips ...Clip) Timeline {
	l.tracks = append(append([][]Clip(nil), l.tracks...), clips)
	return l
}
func (l Timeline) Selected(r *ClipRef) Timeline { l.selected = r; return l }
func (l Timeline) Cursor(on bool) Timeline      { l.cursor = on; return l }
func (l Timeline) SizeCells(w, h int) Timeline  { l.width, l.height = w, h; return l }

// TimelineX is the column of time t in a timeline w wide.
func TimelineX(t, duration float64, w int) int {
	return min(int(math.Max(math.Round(t/duration*float64(w)), 0)), sub(w, 1))
}

// TimelineTimeAt is the time at a column.
func TimelineTimeAt(col int, duration float64, w int) float64 {
	return clamp(float64(col)/float64(max(w, 1))*duration, 0, duration)
}

// HandleTimelineKey: Left/Right ±1 s (Shift: a labeled step), Home/End. True when handled.
func HandleTimelineKey(position *float64, duration float64, cols int, msg tea.KeyPressMsg) bool {
	step := 1.0
	if msg.Mod&tea.ModShift != 0 {
		step = RulerStep(duration, cols)
	}
	var p float64
	switch msg.Code {
	case tea.KeyLeft:
		p = *position - step
	case tea.KeyRight:
		p = *position + step
	case tea.KeyHome:
		p = 0
	case tea.KeyEnd:
		p = duration
	default:
		switch keyOf(msg) {
		case "h":
			p = *position - step
		case "l":
			p = *position + step
		default:
			return false
		}
	}
	*position = clamp(p, 0, duration)
	return true
}

// trimCap is a trim cap at (x, y), h rows: accent cells, the grip `│` in the background on the
// middle row(s) (40% of the height, at least one row; 60% hot).
func trimCap(t *Theme, buf *Buffer, x, y, h int, hot bool) {
	k, frac := t.Accent(), 0.4
	if hot {
		k, frac = t.HotAccent(), 0.6
	}
	omarchy.Fill(t, buf, rect(x, y, 1, h), k)
	g := max(1, min(h, int(math.Round(float64(h)*frac))))
	top := y + (h-g)/2
	for dy := range g {
		omarchy.Put(t, buf, x, top+dy, "│", t.Bg())
	}
}

func (l Timeline) Size(*Theme) (int, int) { return l.width, l.height }

func (l Timeline) Draw(t *Theme, area Rect, buf *Buffer) {
	c := &t.Shown().Controls
	w := area.W
	step := RulerStep(l.duration, w)
	tenths := step < 1
	minor := step / 5
	// Ticks first, then labels over the label row.
	nMinor := int(math.Floor(l.duration / minor))
	for i := 0; i <= nMinor; i++ {
		x := area.X + TimelineX(float64(i)*minor, l.duration, w)
		sym := "╷"
		if i%5 == 0 {
			sym = "│"
		}
		omarchy.Put(t, buf, x, area.Y+1, sym, t.Faint())
	}
	nMajor := int(math.Floor(l.duration / step))
	for i := 0; i <= nMajor; i++ {
		tm := float64(i) * step
		label := ClockLabel(tm, tenths)
		lw := width(label)
		cx := TimelineX(tm, l.duration, w) - (lw-1)/2
		lx := max(0, min(cx, w-lw))
		omarchy.Text(t, buf, area.X+lx, area.Y, label, lw, t.Dim())
	}
	lanesH := sub(area.H, 2)
	n := max(len(l.tracks), 1)
	laneH := lanesH / n
	for ti, clips := range l.tracks {
		ly := area.Y + 2 + ti*laneH
		lh := laneH
		if ti == n-1 {
			lh = lanesH - laneH*(n-1)
		}
		if lh == 0 || w < 2 {
			continue
		}
		omarchy.Fill(t, buf, rect(area.X, ly, w, lh), c.Normal.Fill)
		for ci, clip := range clips {
			x0 := TimelineX(clip.Start, l.duration, w)
			x1 := max(TimelineX(clip.End, l.duration, w), x0+2)
			cw := min(x1-x0, w-x0)
			selected := l.selected != nil && *l.selected == ClipRef{ti, ci}
			r := rect(area.X+x0, ly, cw, lh)
			if selected {
				omarchy.Fill(t, buf, r, c.Selected.Fill)
				trimCap(t, buf, r.X, ly, lh, false)
				trimCap(t, buf, r.Right()-1, ly, lh, false)
			} else {
				omarchy.Fill(t, buf, r, c.Normal.Fill)
				for dy := range lh {
					omarchy.Edges(t, buf, r.X, ly+dy, cw, some(c.Normal.Border.Color))
				}
			}
			omarchy.TextFit(t, buf, r.X+2, ly+(lh-1)/2, clip.Label, sub(cw, 4), bold(t.Fg(), selected))
			t.Hit("clip-"+strconv.Itoa(ti)+"-"+strconv.Itoa(ci), r)
		}
	}
	px := area.X + TimelineX(l.position, l.duration, w)
	head := t.Accent()
	if l.cursor {
		head = t.HotAccent()
	}
	omarchy.Put(t, buf, px, area.Y+1, "▼", head)
	for dy := 2; dy < area.H; dy++ {
		omarchy.Put(t, buf, px, area.Y+dy, "│", head)
	}
	t.Hit("timeline-ruler", rect(area.X, area.Y, w, 2))
}

// ---------------------------------------------------------------------------------------------
// Scrubber

// Scrubber is `0:42 ━━━━━━●────── 2:10`: the times in textDim, the Slider's track (no edges) a
// column from each (keyed "scrubber"). The knob is never bold: the cursor has no cell form (the
// GUI knob's 1.15 scale).
type Scrubber struct {
	position, duration float64
	width              int
}

func NewScrubber(position, duration float64) Scrubber {
	return Scrubber{position: position, duration: math.Max(duration, 0.001), width: 45}
}

func (s Scrubber) Width(w int) Scrubber { s.width = w; return s }

// Cursor is taken for the GUI bindings' API; it draws nothing (the hot knob's scale has no cell
// form).
func (s Scrubber) Cursor(bool) Scrubber { return s }

// HandleScrubberKey: Left/Right ±5 s (Shift ±30 s), Home/End. True when handled.
func HandleScrubberKey(position *float64, duration float64, msg tea.KeyPressMsg) bool {
	step := 5.0
	if msg.Mod&tea.ModShift != 0 {
		step = 30
	}
	var p float64
	switch {
	case msg.Code == tea.KeyLeft || keyOf(msg) == "h":
		p = *position - step
	case msg.Code == tea.KeyRight || keyOf(msg) == "l":
		p = *position + step
	case msg.Code == tea.KeyHome:
		p = 0
	case msg.Code == tea.KeyEnd:
		p = duration
	default:
		return false
	}
	*position = clamp(p, 0, duration)
	return true
}

func (s Scrubber) Size(*Theme) (int, int) { return s.width, 1 }

func (s Scrubber) Draw(t *Theme, area Rect, buf *Buffer) {
	a, b := ClockLabel(s.position, false), ClockLabel(s.duration, false)
	aw, bw := width(a), width(b)
	omarchy.Text(t, buf, area.X, area.Y, a, aw, t.Dim())
	omarchy.Text(t, buf, sub(area.Right(), bw), area.Y, b, bw, t.Dim())
	bar := rect(area.X+aw+1, area.Y, sub(area.W, aw+bw+2), 1)
	w := bar.W
	if w < 2 {
		return
	}
	k := int(math.Round(clamp(s.position/s.duration, 0, 1) * float64(w-1)))
	for i := range w {
		if i < k {
			omarchy.Put(t, buf, bar.X+i, bar.Y, "━", t.Fg())
		} else if i > k {
			omarchy.Put(t, buf, bar.X+i, bar.Y, "─", t.Track())
		}
	}
	omarchy.Put(t, buf, bar.X+k, bar.Y, "●", t.Fg())
	t.Hit("scrubber", bar)
}

// ---------------------------------------------------------------------------------------------
// TrimHandle, CropFrame, DragHandle, DropIndicator

// Edge is which edge or corner a handle is on.
type Edge int

const (
	EdgeNone Edge = iota
	EdgeLeft
	EdgeRight
	EdgeTop
	EdgeBottom
	EdgeTopLeft
	EdgeTopRight
	EdgeBottomLeft
	EdgeBottomRight
)

// TrimHandle is a draggable in/out handle. Trim (left/right): an accent cap a column wide,
// Height rows, with the grip cut in the background. Crop: the heavy accent glyphs a CropFrame
// draws (┏━, ━━, ┃).
type TrimHandle struct {
	edge         Edge
	crop, cursor bool
	height       int
}

func NewTrimHandle(edge Edge) TrimHandle { return TrimHandle{edge: edge, height: 3} }
func NewCropHandle(edge Edge) TrimHandle { return TrimHandle{edge: edge, crop: true, height: 1} }

func (h TrimHandle) Cursor(on bool) TrimHandle { h.cursor = on; return h }
func (h TrimHandle) Height(n int) TrimHandle   { h.height = n; return h }

type cropCell struct {
	dx, dy int
	sym    string
}

func cropCells(e Edge) []cropCell {
	switch e {
	case EdgeTopLeft:
		return []cropCell{{0, 0, "┏"}, {1, 0, "━"}, {0, 1, "┃"}}
	case EdgeTopRight:
		return []cropCell{{0, 0, "┓"}, {-1, 0, "━"}, {0, 1, "┃"}}
	case EdgeBottomLeft:
		return []cropCell{{0, 0, "┗"}, {1, 0, "━"}, {0, -1, "┃"}}
	case EdgeBottomRight:
		return []cropCell{{0, 0, "┛"}, {-1, 0, "━"}, {0, -1, "┃"}}
	case EdgeTop, EdgeBottom:
		return []cropCell{{0, 0, "━"}, {1, 0, "━"}}
	}
	return []cropCell{{0, 0, "┃"}}
}

func (h TrimHandle) Size(*Theme) (int, int) {
	if !h.crop {
		return 1, h.height
	}
	switch h.edge {
	case EdgeTop, EdgeBottom:
		return 2, 1
	case EdgeLeft, EdgeRight:
		return 1, 1
	}
	return 2, 2
}

func (h TrimHandle) Draw(t *Theme, area Rect, buf *Buffer) {
	if !h.crop {
		trimCap(t, buf, area.X, area.Y, area.H, h.cursor)
		return
	}
	k := t.Accent()
	if h.cursor {
		k = t.HotAccent()
	}
	ax, ay := area.X, area.Y
	switch h.edge {
	case EdgeTopRight:
		ax = area.Right() - 1
	case EdgeBottomLeft:
		ay = area.Bottom() - 1
	case EdgeBottomRight:
		ax, ay = area.Right()-1, area.Bottom()-1
	}
	for _, c := range cropCells(h.edge) {
		omarchy.Put(t, buf, ax+c.dx, ay+c.dy, c.sym, bold(k, true))
	}
}

// CropFrame is a crop rectangle: a light accent outline, rule-of-thirds guides (┆, ┄ in track)
// and the eight crop handles in heavy accent strokes inside its edges.
type CropFrame struct {
	width, height int
	cursor        Edge
}

func NewCropFrame(w, h int) CropFrame { return CropFrame{width: w, height: h} }

func (f CropFrame) Cursor(e Edge) CropFrame { f.cursor = e; return f }

func (f CropFrame) Size(*Theme) (int, int) { return f.width, f.height }

func (f CropFrame) Draw(t *Theme, area Rect, buf *Buffer) {
	w, h := area.W, area.H
	if w < 4 || h < 3 {
		return
	}
	for i := 1; i < 3; i++ {
		gx := area.X + (w*i)/3
		for dy := 1; dy < h-1; dy++ {
			omarchy.Put(t, buf, gx, area.Y+dy, "┆", t.Track())
		}
	}
	for i := 1; i < 3; i++ {
		gy := area.Y + (h*i)/3
		for dx := 1; dx < w-1; dx++ {
			x := area.X + dx
			sym := "┄"
			if x == area.X+w/3 || x == area.X+(w*2)/3 {
				sym = "┼"
			}
			omarchy.Put(t, buf, x, gy, sym, t.Track())
		}
	}
	// The outline is square: its corners are the corner handles.
	omarchy.HLine(t, buf, area.X, area.Y, w, "─", t.Accent())
	omarchy.HLine(t, buf, area.X, area.Bottom()-1, w, "─", t.Accent())
	for dy := range h {
		omarchy.Put(t, buf, area.X, area.Y+dy, "│", t.Accent())
		omarchy.Put(t, buf, area.Right()-1, area.Y+dy, "│", t.Accent())
	}
	mx, my := area.X+(w-2)/2, area.Y+(h-1)/2
	place := func(e Edge, r Rect) { NewCropHandle(e).Cursor(f.cursor == e).Draw(t, r, buf) }
	place(EdgeTopLeft, area)
	place(EdgeTopRight, area)
	place(EdgeBottomLeft, area)
	place(EdgeBottomRight, area)
	place(EdgeTop, rect(mx, area.Y, 2, 1))
	place(EdgeBottom, rect(mx, area.Bottom()-1, 2, 1))
	place(EdgeLeft, rect(area.X, my, 1, 1))
	place(EdgeRight, rect(area.Right()-1, my, 1, 1))
}

// DragHandle is the grip a row is dragged by: ⠿, textDim, the foreground when hot.
type DragHandle struct{ cursor bool }

func NewDragHandle() DragHandle                { return DragHandle{} }
func (d DragHandle) Cursor(on bool) DragHandle { d.cursor = on; return d }
func (DragHandle) Size(*Theme) (int, int)      { return 1, 1 }
func (d DragHandle) Draw(t *Theme, area Rect, buf *Buffer) {
	k := t.Dim()
	if d.cursor {
		k = t.Fg()
	}
	omarchy.Put(t, buf, area.X, area.Y, "⠿", k)
}

// DropIndicator is where a dragged row will land: `━` in accent across the list, this many
// columns. Cells can't draw between rows, so it takes a row of its own at the drop slot.
type DropIndicator int

func (d DropIndicator) Size(*Theme) (int, int) { return int(d), 1 }
func (d DropIndicator) Draw(t *Theme, area Rect, buf *Buffer) {
	omarchy.HLine(t, buf, area.X, area.Y, area.W, "━", t.Accent())
}

// ---------------------------------------------------------------------------------------------
// ColorPicker

// Hsva is hue (0–1 turns), saturation, value, alpha, all 0–1.
type Hsva struct{ H, S, V, A float64 }

// RGB8 is the 0–255 channels, by the same arithmetic as every binding's picker.
func (c Hsva) RGB8() [3]uint8 {
	h6 := (c.H - math.Floor(c.H)) * 6
	i := math.Floor(h6)
	f := h6 - i
	s, v := c.S, c.V
	p, q, tt := v*(1-s), v*(1-s*f), v*(1-s*(1-f))
	var out [3]float64
	switch remEuclid(int(i), 6) {
	case 0:
		out = [3]float64{v, tt, p}
	case 1:
		out = [3]float64{q, v, p}
	case 2:
		out = [3]float64{p, v, tt}
	case 3:
		out = [3]float64{p, q, v}
	case 4:
		out = [3]float64{tt, p, v}
	default:
		out = [3]float64{v, p, q}
	}
	var b [3]uint8
	for k, x := range out {
		b[k] = uint8(clamp(math.Round(x*255), 0, 255))
	}
	return b
}

func (c Hsva) Rgba() Rgba {
	b := c.RGB8()
	return Rgba{R: b[0], G: b[1], B: b[2], A: uint8(math.Round(c.A * 255))}
}

// Hex is #rrggbb, or #rrggbbaa below full alpha.
func (c Hsva) Hex() string {
	b := c.RGB8()
	out := fmt.Sprintf("#%02x%02x%02x", b[0], b[1], b[2])
	if c.A < 1 {
		out += fmt.Sprintf("%02x", uint8(math.Round(c.A*255)))
	}
	return out
}

// HsvaFromHex reads #rrggbb[aa]; the hue is kept from keep when the color is gray.
func HsvaFromHex(hex string, keep float64) (Hsva, bool) {
	c, ok := omarchy.ParseHex(hex)
	if !ok {
		return Hsva{}, false
	}
	r, g, b := float64(c.R)/255, float64(c.G)/255, float64(c.B)/255
	mx, mn := math.Max(r, math.Max(g, b)), math.Min(r, math.Min(g, b))
	d := mx - mn
	var h float64
	switch {
	case d == 0:
		h = keep
	case mx == r:
		h = math.Mod(math.Mod((g-b)/d, 6)+6, 6) / 6
	case mx == g:
		h = ((b-r)/d + 2) / 6
	default:
		h = ((r-g)/d + 4) / 6
	}
	s := 0.0
	if mx != 0 {
		s = d / mx
	}
	return Hsva{h, s, mx, float64(c.A) / 255}, true
}

// ColorPicker is an HSV picker in half-block pixels (spec/terminal.md, Editing): the square, a
// hue strip and an alpha strip with their knobs, the preview and a hex TextField, then the theme
// palette's swatches (keyed `swatch-<i>`; the square is "picker-square").
type ColorPicker struct {
	color         Hsva
	noAlpha       bool
	width, height int
	palette       []string
	cursor        bool
	hex           *string
}

func NewColorPicker(c Hsva) ColorPicker {
	return ColorPicker{color: c, width: 38, height: 16, palette: []string{"red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"}}
}

func (p ColorPicker) Alpha(on bool) ColorPicker           { p.noAlpha = !on; return p }
func (p ColorPicker) SizeCells(w, h int) ColorPicker      { p.width, p.height = w, h; return p }
func (p ColorPicker) Palette(names ...string) ColorPicker { p.palette = names; return p }
func (p ColorPicker) Cursor(on bool) ColorPicker          { p.cursor = on; return p }

// Hex is the hex field's text (else the color's hex).
func (p ColorPicker) Hex(s string) ColorPicker { p.hex = &s; return p }

// PaletteColors are the named theme colors (missing keys skipped).
func PaletteColors(t *Theme, names []string) []Rgba {
	pal := &t.Shown().Palette
	var out []Rgba
	for _, n := range names {
		switch n {
		case "foreground":
			out = append(out, pal.Foreground)
		case "background":
			out = append(out, pal.Background)
		case "accent":
			out = append(out, pal.Accent)
		default:
			if c, ok := pal.Named[n]; ok {
				out = append(out, c)
			}
		}
	}
	return out
}

// HandleColorPickerKey: arrows move the square's knob 1% (Shift 10%). True when handled.
func HandleColorPickerKey(c *Hsva, msg tea.KeyPressMsg) bool {
	d := 0.01
	if msg.Mod&tea.ModShift != 0 {
		d = 0.1
	}
	k := keyOf(msg)
	switch {
	case msg.Code == tea.KeyLeft || k == "h":
		c.S = clamp(c.S-d, 0, 1)
	case msg.Code == tea.KeyRight || k == "l":
		c.S = clamp(c.S+d, 0, 1)
	case msg.Code == tea.KeyUp || k == "k":
		c.V = clamp(c.V+d, 0, 1)
	case msg.Code == tea.KeyDown || k == "j":
		c.V = clamp(c.V-d, 0, 1)
	default:
		return false
	}
	return true
}

func (p ColorPicker) Size(*Theme) (int, int) { return p.width, p.height }

func (p ColorPicker) Draw(t *Theme, area Rect, buf *Buffer) {
	strips := 6
	if p.noAlpha {
		strips = 3
	}
	sqW, sqH := sub(area.W, strips), sub(area.H, 5)
	if sqW < 2 || sqH < 1 {
		return
	}
	ph := sqH * 2
	den := func(n int) float64 { return float64(max(n, 1)) }
	pixel := func(i, j int) Rgba {
		return Hsva{p.color.H, float64(i) / den(sqW-1), 1 - float64(j)/den(ph-1), 1}.Rgba()
	}
	for row := range sqH {
		for i := range sqW {
			x, y := area.X+i, area.Y+row
			omarchy.Fill(t, buf, rect(x, y, 1, 1), pixel(i, row*2+1))
			omarchy.Put(t, buf, x, y, "▀", pixel(i, row*2))
		}
	}
	opaque := p.color
	opaque.A = 1
	picked := opaque.Rgba()
	kx := area.X + int(math.Round(clamp(p.color.S, 0, 1)*float64(sqW-1)))
	ky := area.Y + int(math.Round((1-clamp(p.color.V, 0, 1))*float64(sqH-1)))
	omarchy.Fill(t, buf, rect(kx, ky, 1, 1), t.Bg())
	omarchy.Put(t, buf, kx, ky, "●", picked)
	strip := func(x int, color func(j int) Rgba) {
		for row := range sqH {
			for k := range 2 {
				c := color(row*2 + k)
				for dx := range 2 {
					if k == 0 {
						omarchy.Put(t, buf, x+dx, area.Y+row, "▀", c)
					} else {
						omarchy.Fill(t, buf, rect(x+dx, area.Y+row, 1, 1), c)
					}
				}
			}
		}
	}
	knob := func(x, y int) {
		omarchy.Fill(t, buf, rect(x, y, 2, 1), t.Bg())
		omarchy.Put(t, buf, x, y, "◀", t.Fg())
		omarchy.Put(t, buf, x+1, y, "▶", t.Fg())
	}
	// Hue strip: red at the top through the hues back to red.
	hx := area.X + sqW + 1
	strip(hx, func(j int) Rgba { return Hsva{float64(j) / den(ph-1), 1, 1, 1}.Rgba() })
	knob(hx, area.Y+int(math.Round(math.Mod(math.Mod(p.color.H, 1)+1, 1)*float64(sqH-1))))
	if !p.noAlpha {
		ax := hx + 3
		under := omarchy.Over(t.Track(), t.Bg())
		strip(ax, func(j int) Rgba {
			return omarchy.Over(picked.WithAlpha(1-float64(j)/den(ph-1)), under)
		})
		knob(ax, area.Y+int(math.Round((1-clamp(p.color.A, 0, 1))*float64(sqH-1))))
	}
	// Preview and hex.
	fy := area.Y + sqH + 1
	shown := omarchy.Over(p.color.Rgba(), t.Bg())
	omarchy.Put(t, buf, area.X, fy, "█", shown)
	omarchy.Put(t, buf, area.X+1, fy, "█", shown)
	hex := p.color.Hex()
	if p.hex != nil {
		hex = *p.hex
	}
	NewTextField(hex).Width(sub(area.W, 3)).Cursor(p.cursor).Draw(t, rect(area.X+3, fy, sub(area.W, 3), 1), buf)
	// Palette.
	py := fy + 2
	for i, c := range PaletteColors(t, p.palette) {
		x := area.X + i*3
		if x+2 > area.Right() {
			break
		}
		omarchy.Put(t, buf, x, py, "█", c)
		omarchy.Put(t, buf, x+1, py, "█", c)
		if c.R == picked.R && c.G == picked.G && c.B == picked.B {
			omarchy.Put(t, buf, x, py+1, "▔", t.Fg())
			omarchy.Put(t, buf, x+1, py+1, "▔", t.Fg())
		}
		t.Hit("swatch-"+strconv.Itoa(i), rect(x, py, 2, 1))
	}
	t.Hit("picker-square", rect(area.X, area.Y, sqW, sqH))
}
