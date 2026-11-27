# Lists

Rows, the keyboard cursor that walks them, the scroll view they sit in, and the small pieces rows and grids are made of. Every shell panel builds these by hand today (roadmap Tier 1 #1, #2, #3, #7, #8, #10, #11, #12). Token names refer to `spec/tokens.md`; `space(n)` is `Style.space(n)` / `Theme::space(n)`.

Tones, wherever a component takes one: neutral (the component's own ink), `accent`, `success`, `warning`, `urgent`, `muted`. Qt takes them as strings (`""` for neutral), Rune as `Tone::{Neutral, Accent, Success, Warning, Urgent, Muted}`. The feedback components ([`feedback.md`](feedback.md)) name the same colors by what they mean instead: `info` is `accent` and `error` is `urgent`. A binding with one tone type accepts both spellings.

## ListRow

Qt `ListRow` (on `CursorSurface`) · Rune `ListRow`

One row of a keyboard-and-mouse list: what the network, bluetooth, tailscale and audio panels each build (NetworkRow, DeviceRow, ...).

**Anatomy.** Leading slot (a glyph, or any item such as a small Avatar) · info column (bold title; dim subtitle; status line) · trailing (value text, or any item such as a Badge or a non-interactive ToggleSwitch) · end slot (a trailing glyph that stays, such as a lock, signal bars, a drill-in chevron or a check; and the action, revealed in its place under the cursor or the pointer).

**API.**

| Qt | Rune | |
|---|---|---|
| `glyph`, `glyphSize`, `glyphColor` | `.glyph(g)`, `.glyph_color(c)` | Leading Nerd Font glyph, `font.title` |
| `leading: Component` | `.leading(El)` | Any leading item instead |
| `title`, `subtitle` | `new(title)`, `.subtitle(s)` | |
| `status`, `statusTone` | `.status(s, tone)` | Status line, dim unless toned |
| `value` | `.value(v)` | Dim trailing text |
| `trailing: Component` | `.trailing(El)` | Any trailing item instead |
| `trailingGlyph`, `trailingTone` | `.trailing_glyph(g, tone)` | A glyph that stays in the end slot (the lock), `textDim` unless toned |
| `actionGlyph`, `actionUrgent`, `actionTip` | `.action(g, urgent)` | The revealed action (forget), in the end slot |
| `expansion: Component` | `.expansion(El)` | Opens the row: any item across it under the main line (a password field and Connect) |
| `actionCursor` | `.action_cursor(bool)` | The cursor is on the action, not the row |
| `hasCursor`, `current`, `disabled` | `.cursor(b)`, `.current(b)`, `.disabled(b)` | |
| `clicked()`, `actionClicked()` | `.on_press(f)`, `.on_action(f)` | |
| `pointerMoved(mouse)`, `hovered()` | `.on_pointer_move(f(s, x, y))` | Hand to `CursorModel.pointerMoved` |
| | `.key(k)`, `.width(w)` | |

**States.** The look follows `hasCursor` and `current` only, never hover (CursorSurface's contract): the pointer reaches the app through `pointerMoved`, which moves the CursorModel, so there is one highlight on screen.

| State | Fill | Border | Text |
|---|---|---|---|
| normal | none | none | title foreground, subtitle `textDim`, status tone (else `textDim`) |
| cursor (wins over current) | `controls.hover.fill` | `controls.hover.border` | as normal |
| current | `controls.selected.fill` | `controls.selected.border` | as normal |
| disabled | none | none | everything `textFaint`; no clicks, no pointer, no action |

The action glyph shows while the row has the cursor or the pointer, or when `actionCursor` is set; with `actionCursor` its slot takes the cursor look in its own ink (urgent when `actionUrgent`): the ink at the hover fill alpha, a border at the hover border alpha and width.

The trailing glyph and the action share the end slot, as the network panel's lock and forget do: the trailing glyph shows at rest, and the action replaces it (never beside it) whenever the action shows. A row with a trailing glyph and no action shows the glyph always; a disabled row shows it in `textFaint` and never shows the action. Clicks on a bare trailing glyph are the row's.

**Metrics.** Radius `radius`. Padding `controlPaddingX` across, `rowPaddingX / 2` down (height = content + `rowPaddingX`). Leading slot `font.iconLarge` wide, glyph centered in it by its box, then `space(10)`. Title `body` bold; subtitle and status `caption`; lines `space(1)` apart; all elide. Trailing items `space(8)` from the info and from each other; value `bodySmall`. End slot `space(22)` square, last on the line, `space(8)` after any value or trailing item, present when the row has a trailing glyph or an action; the trailing glyph and the action glyph are both `subtitle`, centered in it by their box; the trailing glyph `textDim` (or its tone), the action `textDim` (or `urgent`). The slot is reserved while empty, so revealing the action never reflows the row. An expansion sits `space(4)` under the main line, inset `controlPaddingX` each side, inside the row's fill; the main line then sits at the top padding instead of centering.

## CursorModel

Qt `CursorModel` (non-visual `QtObject`, pairs with `PanelKeyCatcher` or `Keys.onPressed`) · Rune `CursorModel` (plain struct) and `printable(&KeyEvent)`

The keyboard cursor every panel re-implements: a section and a row within it, plus whether it shows.

**API.**

| Qt | Rune | |
|---|---|---|
| `sections: [{ name, count }]`, `setCount(name, n)` | `new([(name, count)])`, `list(count)`, `set_count(name, n)` | |
| `focusSection`, `selectedIndex`, `cursorActive` | `section()`, `index()`, `active` | |
| `columns`, `pageSize` | `.columns(n)`, `page` | Grid width; Page Up/Down step (5) |
| `isAt(section, index)` | `is_at(section, index)`, `cursor_in(section)` | A row's cursor flag |
| `set(section, index)`, `reset()` | `set(..)`, `reset()` | |
| `move(dx, dy)`, `moveSection(d)`, `page(d)`, `home()`, `end()` | `move_by`, `move_section`, `page_by`, `home`, `end` | |
| `handleKey(event)` → handled | `apply_key(&KeyEvent)` → `CursorKey` | |
| `printable(event)` → text or `""` | `printable(&KeyEvent)` → `Option<&str>` | Type-ahead filter text |
| `pointerMoved(item, mouse, section, index)`, `referenceItem` | `pointer_moved(x, y, section, index)`, `reset_pointer()` | PointerMoveGate semantics |
| `allowInitialSample()` | `allow_initial_sample()` | |
| `moved(section, index, byKey)`, `activated`, `deleteRequested`, `closeRequested`, `textTyped(text)` | `CursorKey::{Moved, Activate, Delete, Close, Text(t), Ignored}` | |

**Keys.** Down/`j`, Up/`k` move a row, crossing into the next or previous non-empty section at its ends and stopping at the first and last row. Right/`l`, Left/`h` move a column within the line when `columns > 1` (a grid; Down from a short last line's line above lands on its last tile), else nothing. Page Down/Up move `pageSize` lines within the section. Home/End go to the first row of the first non-empty section, or the last row of the last. Tab/Shift+Tab jump to the next or previous section's first row. Return, Enter and Space activate; Escape closes; Delete and `x` delete. Any other printable key without Ctrl, Alt or Super is filter text. The first key after the cursor was hidden only shows it, as the panels do.

**Pointer.** A pointer sample moves the cursor only when it travelled more than 1 point since the last accepted one, so rows scrolling or re-laying out under a still pointer don't steal the keyboard's cursor. Keyboard moves reset the gate; the next sample only primes it. On a keyboard move, reveal the row: Qt `ScrollArea.ensureVisible(item)` from `moved(.., byKey)`, Rune `ScrollArea::ensure_visible(tx, row_key)` on `CursorKey::Moved`.

## ScrollArea

Qt `ScrollArea` · Rune `ScrollArea`

A vertical scroll view capped at a maximum height, with Omarchy's thin scrollbar beside it.

**API.** Qt: `maxHeight`, `spacing`, children (a Column), `ensureVisible(item)`, `ensureVisibleRange(y, h)`, `flickable`, `contentColumn`, `overflowing`. Rune: `new(key)`, `.max_height(h)`, `.width(w)`, `.gap(g)`, `.child(..)`/`.children(..)`, `ScrollArea::ensure_visible(tx, row_key)`.

**Anatomy and states.** The content, as wide as the area, and over its right edge (nothing reserved, as the shell's `AsNeeded` bars) a bar `scrollbarThickness` wide and the viewport's height: a groove in `track` and a thumb in the normal border color (foreground at the normal border alpha), `textDim` under the pointer or while dragged. Both `min(4, radius)`. The bar shows only while the content overflows. Overlaying it rather than reserving a gutter keeps a short list as wide as its siblings, so row fills reach the panel's content edge, and nothing reflows when the content starts or stops overflowing (a gutter reserved only on overflow would narrow every row the moment an expansion pushed the list past its cap). Children keep their content clear of the bar with their own right padding of at least `scrollbarThickness`: ListRow's `controlPaddingX`. The thumb is at least `space(24)` long and drags; scrolling stops at the ends (no overshoot). The Rune bar is drawn (rune-kit's ScrollBar has no groove and overlays at rest), driven by the viewport's scroll offset through live values.

## KeyValue

Qt `KeyValue` (a `Grid`) · Rune `KeyValue`

Dim labels beside their values in a grid, `n` pairs to a line: the details block of the power, dropbox and network panels (InfoLabel, InfoValue).

**API.** Qt: `pairs: [{ label, value, tone }]`, `pairColumns` (Grid owns `columns`). Rune: `.pair(label, value)`, `.pair_toned(label, value, tone)`, `.columns(n)`.

**Metrics.** Text `bodySmall`; labels `textDim`, values foreground (or their tone). Each grid column as wide as its widest cell, `space(20)` apart; lines `labelGap` apart.

## Hero

Qt `Hero` (the hand-made heroes' layout; `PanelHero` stays for its slot-based callers) · Rune `Hero`

The head of a panel: a big glyph, a bold title, a dim status line and a trailing control.

**API.** Qt: `glyph`, `title`, `status`, `error`, `glyphOpacity`, `trailing: Component`. Rune: `new(glyph, title)`, `.status(s)`, `.error(b)`, `.glyph_opacity(o)`, `.trailing(El)`.

**Metrics.** Glyph `display` centered by ink in a `display`-wide slot (omitted when empty), then `space(14)`. Title `title` bold; status `caption` bold, uppercased, letter spacing 1.2, `textDim` (`urgent` with `error`); `space(2)` apart; both elide. The trailing control sits `space(12)` after the labels, centered against them. Height is the tallest of the three.

## Badge

Qt `Badge` · Rune `Badge`

A count or short label in a small pill.

**API.** Qt: `text`, `tone`, `filled`. Rune: `new(label)`, `.tone(t)`, `.filled(b)`.

**Looks.** Neutral: foreground glass (`controls.normal` fill and border). Toned: the tone at the selected fill alpha, text in the tone. Filled: the tone solid, text in `background`.

**Metrics.** Text `caption` bold. Padding `space(6)` across and `space(1)` down, each plus the normal border width (reserved for every tone, so badges line up). Never narrower than tall. Radius `min(4, radius)`.

## StatusDot

Qt `StatusDot` · Rune `StatusDot`

A dot ringed in the surface color. Always a circle.

**API.** Qt: `tone`, `size`, `ring`, `ground`. Rune: `new(tone)`, `.size(px)`, `.ground(c)`, `StatusDot::ring(t)`.

**Metrics.** Dot `space(8)`, ring `max(1, space(2))` in `ground` (default the popups background). Docked on an Avatar (`status`): dot `max(space(6), round(size × 0.28))`, its ring box at the avatar's bottom-right corner, overhanging by the ring.

## Avatar / AppIcon

Qt `Avatar` · Rune `Avatar`

A person's or an app's picture with fallbacks, tried in order: the image, an icon from the icon theme by name, a glyph, the first letter of the name. A source that fails to load gives way to the next.

**API.** Qt: `name`, `source` (URL), `iconName`, `glyph`, `size` (`"small"`, `"medium"`, `"large"`) or `pixelSize`, `round`, `status` (docks a StatusDot), `ground`. Rune: `new(name)`, `.image(Option<&rune::Image>)` (from an `ImageCache`; undecoded or failed draws the fallback), `.icon(name)`, `.glyph(g)`, `.size(AvatarSize)` or `.size_px(px)`, `.round(b)`, `.status(tone)`, `.ground(c)`, `Avatar::initial(name)`.

**Metrics.** Small `font.iconLarge` (fills a ListRow's leading slot), medium `font.displayLarge`, large `font.display × 2`. Corners `min(radius, size / 2)`, square at Omarchy's default 0, as Toast's thumbnails; `round` makes a disc, for people, as elsewhen's avatars. The image covers the square (cropping its long side), clipped to the corners (Qt draws it on a Canvas so it renders without shader effects). Fallback tile `controls.selected.fill`, mark `round(size × 0.5)` in the foreground: the initial bold, a glyph regular. An icon is drawn whole instead, fitted into the square (its aspect kept), with no tile, no corners and no clipping: its artwork has its own shape.

**Icons by name.** `iconName` / `.icon(name)` takes a freedesktop icon name (`firefox`, `org.gnome.Nautilus`; an absolute path to a file also works) and resolves it the same way in every binding: Rust `omarchy_theme::icons::IconLookup` (any Rust binding can use it; Rune through `Theme::icon(name, size)`), Qt the `IconLookup` singleton in `Omarchy.Theme.Native` (`IconLookup.path(name, size)`, `IconLookup.url(name, size)`, `IconLookup.theme`, `IconLookup.themes`). `bin/test` checks that both resolve every fixture icon to the same file.

1. The icon theme is the one the Omarchy theme names in `current/theme/icons.theme` (its first non-empty line, such as `Yaru-magenta`), else `Yaru-blue`: what `omarchy-theme-set-gnome` hands to GNOME's `icon-theme`. `OMARCHY_UI_ICON_THEME` pins it.
2. Search that theme, then its `index.theme` `Inherits` chain depth-first (themes that aren't installed are skipped, none twice), then `hicolor`. A theme's `index.theme` comes from the first root that has one; its directories are searched in every root.
3. Roots, in order: `$XDG_DATA_HOME/icons` (`~/.local/share/icons`), each `$XDG_DATA_DIRS/*/icons` (`/usr/local/share:/usr/share`), `~/.icons`, `/usr/share/pixmaps`. `OMARCHY_UI_ICON_DIRS` (colon-separated) replaces them.
4. Within a theme, an SVG wins, from a `Scalable` directory first; else the PNG from the smallest directory at or above the requested size (in device pixels; a directory's size is `Size × Scale`), else the largest; the first in the theme's `Directories` order on a tie. The first theme in the chain that has the icon at any size answers.
5. No theme has it: `<root>/<name>.svg` or `.png` in a root (pixmaps). Nothing: the glyph or the initial.
6. App icons first, as the shell's app library (`AppLibrary.iconSource`) has it: steps 2 and 4 look first only in app and device directories (`Context=Applications` or `Devices`; with no `Context`, a path with an `apps` or `devices` part), then step 5, then steps 2 and 4 again in every context. So `zoom` is the Zoom app's icon even where the theme has a `zoom` action (a magnifier), and an icon only in another context is still found.

The request is the avatar's size in device pixels, and the file is rasterized at that size (Qt `sourceSize`, Rune `ImageCache::file(path, size, scale)`), so SVGs stay crisp. Answers are cached per icon theme. A theme switch that names another icon theme (or an edit to `icons.theme`) re-resolves: Rust carries the name in `Tokens::icon_theme` (not in the canonical JSON), so `omarchy_theme::watch` delivers it; Qt's `ThemeSource.iconTheme` notifies `IconLookup`. Both also watch the theme directory itself, re-armed after a switch. In Rune, a found icon decodes off the view thread and holds its square (blank) until it has; with `Theme::follow` the app re-renders when it has.

## TileGrid

Qt `TileGrid` (a `Grid` of CursorSurfaces) · Rune `TileGrid` with `Tile`

A grid of tiles, each a glyph or an Avatar above a label of up to two lines: the emoji picker and app grid.

**API.** Qt: `tiles: [{ label, glyph } | { label, name, source, glyph }]`, `columns`, `cellWidth`, `cursorIndex`, `currentIndex`, `activated(index)`, `pointerMoved(index, item, mouse)`. Rune: `new([Tile::new(label).glyph(g) | .avatar(Avatar)])`, `.columns(n)`, `.cell_width(w)`, `.cursor(Option<usize>)`, `.current(Option<usize>)`, `.key(k)` (tiles keyed `k-i`), `.on_activate(f(s, i))`, `.on_pointer_move(f(s, i, x, y))`. Drive it with a `CursorModel` with `columns` set.

**States.** As ListRow: the cursor tile takes the hover fill and border, the current one the selected fill.

**Metrics.** Tiles `space(88)` wide, no gap between them, radius `radius`. Every tile reserves `md` padding top and bottom plus `max(line(display), displayLarge)` icon box, `md` gap, and two `bodySmall` label lines so all tiles share one height; inside each tile, the icon box (glyph centered by ink or medium Avatar) + `md` + actual label stack vertically centers within the tile height, with the label `bodySmall`, centered, wrapping to at most two lines and eliding, `md` in from each side.

## Gallery

### `lists`

1. A ScrollArea 420 wide, capped at 340, rows `xxs` apart, its rows driven by a CursorModel (one section `rows` of 9, cursor on row 1; the keys and the pointer move it in the live galleries):
   1. Coffee Shop: glyph U+F0928, subtitle "WPA2", value "5 GHz", trailing glyph the lock U+F033E.
   2. Basecamp Guest: glyph U+F0925, subtitle "Open", under the cursor.
   3. Basecamp: glyph U+F0928, status "Connected" in success, current.
   4. Neighbor 5G: glyph U+F091F, subtitle "Out of range", disabled.
   5. Messages: a small Avatar "Messages" leading, subtitle "From Basecamp", trailing Badge "3" accent filled.
   6. Headphones: glyph U+F00AF, subtitle "Battery 80%", trailing ToggleSwitch on (not interactive).
   7. Office: glyph U+F0922, subtitle "Saved", trailing glyph the lock U+F033E, forget action U+F0159 urgent with the action cursor (so forget shows in the lock's place).
   8. Library: glyph U+F0922, subtitle "WPA2", the lock, forget action U+F0159 urgent (past the cap).
   9. Airport Free WiFi: glyph U+F091F, subtitle "Open" (past the cap).
2. Rows with trailing glyphs that stay, a column 420 wide, `xxs` apart: Appearance (glyph U+F03D8, subtitle "Theme, font, rounding", trailing chevron U+F0142); Display (glyph U+F0379, subtitle "2560 × 1440", chevron U+F0142); Headphones (glyph U+F02CB, subtitle "Output", current, trailing check U+F012C in accent).
3. Hero 420 wide: glyph U+F0928, "Wi-Fi", status "Connected to Basecamp", trailing ToggleSwitch on.
4. Hero 420 wide: glyph U+F00B2 at half opacity, "Bluetooth", status "No adapter found" as an error.
5. KeyValue, two pairs to a line: Ping 18 ms · Packet loss 4% (urgent) · Receiving 1.2 MB/s · Sending 240 KB/s · IP address 192.168.1.24 · Gateway 192.168.1.1.

### `tiles`

1. Badges, `controlGap` apart: "New" (neutral), "3" accent filled, "12" urgent filled, "Online" success, "Low" warning, "Offline" urgent.
2. A row `xxl` apart: StatusDots success, warning, urgent, muted, accent; then medium Avatars with docked dots on the window background: "Ryan" round (success), the sample picture (warning), glyph U+F048B (urgent).
3. Avatars `xxl` apart: "ada" small, "Basecamp" medium, "Omarchy" large round, glyph U+F06A9 medium, the picture small, medium round, large, and a missing file falling back to "M".
4. Avatars by icon name, `xxl` apart, medium unless noted: Files `org.gnome.Nautilus`, Terminal `utilities-terminal`, Firefox `firefox`, Chromium `chromium` small, Steam `steam` large, Btop `btop` small, and Nothing `no-such-app` with glyph U+F120 (no icon anywhere: the glyph). Under `bin/review` they come from the fixture icon tree, so the row shows each step of the lookup: an SVG beside a nearer PNG, the nearest PNG size, an inherited theme, hicolor, pixmaps, an icon of the theme from a second root.
5. A TileGrid of 4 columns: Terminal U+F120, Files U+F07B, Browser U+F0AC (under the cursor), Music U+F001, Settings U+F013, Calendar U+F073, Screen Recorder U+F03D (two lines), Photos (the picture as an Avatar).

The sample picture is `qt/gallery/assets/avatar.png`, the same 96×96 file as `rune/gallery/assets/avatar.png`. The fixture icon tree is `fixtures/icons/`: `system/` holds `Fixture-Accent` (Inherits `Fixture-Base`, which inherits a missing theme and `hicolor`) and `hicolor`, `local/` one more `Fixture-Accent` icon, `pixmaps/` a loose icon, and a `zoom` action in `Fixture-Accent` beside a `zoom` app in `hicolor`, an `edit-copy` action and a `printer` device for step 6; `bin/review` sets `OMARCHY_UI_ICON_DIRS` to those three and `OMARCHY_UI_ICON_THEME=Fixture-Accent`.
