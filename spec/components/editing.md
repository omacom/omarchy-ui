# Editing components

The pieces media and image editors need (roadmap Tier 3): a timeline, a transport scrubber, trim and crop handles, drag-to-reorder grips, and an HSV color picker. Sources: omareel (`Timeline.qml`, `ClipTrack.qml`, `ZoomTrack.qml`, `OmTrimHandle.qml`) and omasnap (`editor.cpp`'s crop and custom color panel), re-expressed in the kit's tokens.

| Component | Qt (`import Omarchy.Ui`) | Rune (`omarchy_rune::ui`) |
|---|---|---|
| Timeline | `Timeline` | `Timeline`, `Clip`, `TimelineView` |
| Scrubber | `Scrubber` | `Scrubber` |
| TrimHandle | `TrimHandle` | `TrimHandle`, `Edge` |
| CropFrame | `CropFrame` | `CropFrame` |
| DragHandle | `DragHandle` | `DragHandle` |
| DropIndicator | `DropIndicator` | `DropIndicator` |
| ColorPicker | `ColorPicker` | `ColorPicker`, `Hsva` |

Shared rules: handles and the playhead are accent, because they belong to the chosen clip or region. Clips, lanes and swatches are foreground glass. Small parts round by `min(4, radius)`. Knobs are the Slider's knob. Every interactive part takes `hasCursor` / `.cursor(true)` for the hot look and works from the keyboard. "Hot accent" means the accent with a quarter of the foreground mixed in (Qt `Qt.tint(accent, alpha(foreground, 0.25))`, Rune `accent.mix(foreground, 0.25)`).

## Timeline

A time ruler over one or more clip tracks, with a playhead.

**Anatomy**

- **Ruler**, `space(24)` tall. Labeled major ticks sit at the first step of 0.1, 0.2, 0.5, 1, 2, 5, 10, 15 or 30 s, or 1, 2, 5, 10, 15 or 30 min, or 1 h that keeps them `space(90)` apart. Major ticks are `space(6)` tall and minor ticks (five per step) `space(3)`. Both are 1px in textFaint, bottom-aligned. Labels are m:ss (tenths below a 1 s step) in `caption` textDim at `xxs` from the top. Each label is centered on its tick and kept inside the ruler at both ends.
- **Lanes** are one per track. They share the height under the ruler, `xs` apart, and are filled with the normal fill at `min(4, radius)`.
- **Clips** sit `xxs` inside their lane. A clip is control glass: the normal look at rest and the hover look under the pointer, at `min(4, radius)`. Its label is `bodySmall` foreground, inset `controlPaddingX`, and elided.
- **Selected clip**: the selected fill, an accent border `max(1, space(2))` wide, a bold label, and a TrimHandle on each end.
- **Playhead**: an accent head `space(10)` square at `min(4, radius)`, sitting on the ruler's bottom edge, and a `max(1, space(2))` line from there to the bottom. It turns hot accent when hot.

Positions are `round((t − viewStart) × width × zoom / duration)`.

**API**

| Qt | Rune | |
|---|---|---|
| `duration`, `position` (s) | `Timeline::new(duration)`, `.position(t)` | |
| `tracks: [[{start, end, label}]]` | `.track(clips)` per track, `Clip::new(start, end, label)` | |
| `selectedTrack`, `selectedClip` | `.selected(Some((track, clip)))` | |
| `zoom` (1 = whole duration), `viewStart`, `maxZoom` (64) | `.zoom(zoom, start)`, `.max_zoom(z)` | |
| `hasCursor` | `.cursor(bool)` | hot playhead |
| implicit `space(720)` × `space(160)` | `.size(w, h)` | |
| `seek(time)` | `.on_seek(\|s, t\|)` | press or drag on the ruler or an empty lane; Left/Right ±1 s (Shift: a major step), Home/End |
| `clipPressed(track, clip)` | `.on_select(\|s, track, clip\|)` | |
| `trimmed(track, clip, edge, time)` | `.on_trim(\|s, track, clip, Edge, t\|)` | drag a trim handle, or nudge it 1pt with arrows (10pt with Shift) |
| `viewChanged(zoom, viewStart)` | `.on_view(\|s, zoom, start\|)` | the wheel zooms ×1.25 per notch around the pointer, clamped to the duration |

**States**: a clip is at rest, hovered or selected. The playhead is at rest or hot (pointer on its head, focus, `hasCursor`).

## Scrubber

A media transport scrub: elapsed time, a position bar and the total time.

- **Times** are `bodySmall` textDim, m:ss (h:mm:ss from an hour), `controlGap` from the bar.
- **Bar**: the Slider's metrics. The track is `max(4, round(controlHeight × 0.11))` tall and round, unplayed in `track` and played in the foreground. The knob is `max(14, round(controlHeight × 0.38))`, foreground, ringed `max(1, space(2))` in the popups background, and scales 1.15 when hot. Height is `max(space(22), knob + md)`.
- **Qt**: `position`, `duration` (s), `hasCursor`, `surfaceColor`, `seek(time)`. Implicit width `space(360)`.
- **Rune**: `Scrubber::new(position, duration)`, `.width(w)`, `.cursor(bool)`, `.on_seek(|s, t|)`.
- **Keys**: Left/Right ±5 s (Shift ±30 s), Home/End.

## TrimHandle

A draggable in/out handle. It comes in two kinds.

- **Trim** (`edge` left or right) is a cap `space(8)` wide and as tall as the clip (default `space(40)`). It is accent, rounded `min(4, radius)` on its outer side and square on the inner side. A grip line `max(1, space(2))` wide is cut into it in the background color, 40% of the height at rest and 60% when hot.
- **Crop** (`crop: true` / `TrimHandle::crop(edge)`) is accent and `space(4)` thick, with corners `min(4, radius, 2)`. A side handle is a bar `space(18)` long. A corner handle is an L bracket `space(18)` on each arm.
- **States**: rest, or hot (pointer, focus, drag, `hasCursor`). Hot turns it hot accent and, on a trim cap, lengthens the grip.
- **Qt**: `edge` (`left`, `right`, `top`, `bottom`, `top-left`, `top-right`, `bottom-left`, `bottom-right`), `crop`, `hasCursor`, `color`, `gripColor`. Signals: `moved(dx, dy)` (travel since the press), `released()`, `nudged(dx, dy)` (arrows: 1px, Shift 10px, along the axes the handle moves).
- **Rune**: `TrimHandle::new(Edge)`, `::crop(Edge)`, `.height(h)`, `.cursor(bool)`, `.on_drag(|s, &mut Drag|)` (keep the start value in `Drag::data` on Down), `.on_nudge(|s, dx, dy|)`, `.size(&theme)`.

## CropFrame

A crop rectangle: a 1px accent outline, rule-of-thirds guides (1px, `track`) and the eight crop TrimHandles inside its edges. Side bars are centered on their side. The frame has no fill, so it sits over the image.

- **Qt**: `cursorEdge` (a handle drawn hot). Signals: `pressed(edge)`, `moved(edge, dx, dy)`, `released(edge)`, `nudged(edge, dx, dy)`. Implicit size `space(240)` × `space(140)`.
- **Rune**: `CropFrame::new(w, h)`, `.cursor(Some(Edge))`, `.on_drag(|s, Edge, &mut Drag|)`.

## DragHandle

The grip a row is dragged by to reorder it: a 2×3 grid of dots `space(3)` square and `space(3)` apart, rounded `min(4, radius, 1.5)`, centered in a `space(16)` × `space(22)` box. The dots are textDim at rest and foreground when hot (pointer, focus, drag, `hasCursor`). The pointer is an open hand, closed while dragging.

- **Qt**: `hasCursor`. Signals: `pressed()`, `moved(dx, dy)`, `released()`, `moveRequested(delta)` (Up/Down while focused).
- **Rune**: `DragHandle::new()`, `.cursor(bool)`, `.on_drag(|s, &mut Drag|)`, `.on_move(|s, ±1|)`.

The list owning the rows works out the drop slot and shows a DropIndicator there.

## DropIndicator

Where a dragged row will land: an accent line `max(1, space(2))` thick (`DropIndicator::thickness`), rounded `min(4, radius, thickness / 2)`, as wide as the list. It is centered on the boundary between two rows and drawn over them, taking no layout space.

- **Qt**: `DropIndicator { width }`.
- **Rune**: `DropIndicator::new(width)`.

## ColorPicker

An HSV picker. Its default size is `space(300)` × `space(260)`.

- **Square**: saturation runs left to right and value top to bottom. It is the hue (s = v = 1) with white fading in from the left and black from the bottom, at `min(4, radius)`. Its knob (`knobSize`) is filled with the picked color (opaque) inside a concentric two-tone ring (1 px `background` outer, 1 px `foreground` inner), clamped inside the square, and scales 1.15 when hot.
- **Hue strip**: `space(14)` wide, `md` right of the square, red, yellow, green, cyan, blue, magenta, red top to bottom. Its knob is `max(8, stripWidth − space(4))` round (1 px `background` outer ring, `foreground` inner), inset `space(2)` from all four edges of the strip.
- **Alpha strip** (optional, default on): `space(14)` wide, `md` right of the hue strip. It shows the opaque color fading to clear, over `track`, opaque at the top, with the same inset knob.
- **Preview and hex**: `lg` below. The preview swatch is a square as tall as the field, in the picked color with alpha, at `radius` with a 1px normal border. Then `md` and a TextField (`#rrggbb`, `#rrggbbaa` below full alpha, lowercase). A valid hex typed into the field applies at once.
- **Palette**: `lg` below that, swatches `space(20)` square and `md` apart, at `min(4, radius)`. They show the theme's `paletteNames`, by default `red orange yellow green cyan blue magenta foreground` from colors.toml, skipping missing keys. A swatch has a 1px normal border, the hover border when hot, and a `max(1, space(2))` foreground border when it is the current color (rgb only). Pressing one picks it and keeps the alpha.

The square and strips take the height that the field and palette leave. Borders paint over what's behind the element, not its own fill, as Qt's Rectangle does.

- **Qt**: `hue`, `saturation`, `value`, `alpha` (0–1), `showAlpha`, `hasCursor`, `surfaceColor`, `paletteNames`, readonly `color` and `hex`, `setColor(colorOrHex)`, `edited(color)`.
- **Rune**: `ColorPicker::new(Hsva)`, `.alpha(bool)`, `.size(w, h)`, `.palette(names)`, `.cursor(bool)`, `.hex(&TextEdit)`, `.on_change(|s| &mut s.hex, |s, Hsva|)`. The picker rewrites the hex text when the color changes elsewhere. `Hsva::from_hex(hex, keep_hue)`, `.hex()`, `.rgb8()`, `.rgba()`. Both bindings convert HSV to 8-bit channels with the same arithmetic, rounding each channel.
- **Keys**: arrows move the square's knob 1% (Shift 10%). Up/Down move a focused strip. Return/Space pick a focused swatch (Qt).

## Gallery

### `timeline`

1. A Timeline 720 × 160, duration 0:30, playhead at 0:12. Track 1 has Intro 0–7.5, Demo 8–18 (selected, so its trim handles show) and Outro 18.5–30. Track 2 has Voiceover 2–12 and Music 14–26.
2. A Scrubber 360 wide at 0:42 of 2:10.
3. A row, `huge` apart, top-aligned:
   - A 360-wide list of four `popupRowHeight` rows: Workspaces, Clock, Battery, Network. Each row has a DragHandle `sm` from the left, then its `body` label `sm` after. Row 2 (Clock) is under the cursor: hover fill at `radius` and a foreground grip. A DropIndicator lies across the boundary between rows 3 and 4.
   - A 240 × 140 rectangle in the normal fill, with a CropFrame over it.

### `color`

1. A ColorPicker 300 × 260 for the fixed color `#7aa2f7` (h 220.8°, s 0.506, v 0.969, alpha 1), with the alpha strip, the hex field and the theme's palette row. The color is fixed rather than the theme accent, so the picker's content is the same in every theme. The chrome (field, ring, palette) follows the theme.
