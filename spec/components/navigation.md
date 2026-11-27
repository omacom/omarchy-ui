# Navigation components

Menus, keys and the chrome apps move around with: Menu and ContextMenu, Keycap and ShortcutSheet, the Tooltip with a shortcut, Toolbar, Tabs and IconRail. Roadmap Tier 2 #14, #15, #16, #18 and #19. ButtonGroup stays the small segmented choice inside forms; Tabs switch whole pages.

Token names are `spec/tokens.md`'s. `space(n)` is `Style.space(n)` / `Theme::space(n)`. "Small radius" is `min(4, radius)`. Text vertically centers on its line box; "centered by ink" means horizontally on the glyph outline's middle (Qt `OpticalGlyph`, Rune `rune::text::ink`) and vertically on the line box rounded to whole points.

## Menu

Qt `Menu` with `MenuItem`, `MenuSeparator`, `MenuSection`; Rune `Menu` with `MenuItem`, `.separator()`, `.section()`.

**Anatomy.** A surface in `surfaces.menu` (background, border spec, `radius`), `sm` inside its border, holding a column of rows:

- **MenuItem**: `[leading glyph] label ··· [shortcut hint] [mark]`.
  - Glyph: `font.icon`, centered by ink in an `icon`-wide box, `controlGap` before the label.
  - Label: `body`.
  - Hint: plain text (no Keycaps), `bodySmall`, `textDim`, at least `huge` after the label, right-aligned on the label's baseline.
  - Mark: in an `icon`-wide box `controlGap` after the hint, centered by ink. A check `✓` (`body`, accent) when checked, a geometric `space(6)` circle dot (accent) when checked and exclusive, or a chevron U+F054 (`iconSmall`, `textDim`) for a submenu row.
  - When any row in the menu has a glyph, every row reserves the glyph column; likewise for the mark column. Labels and hints line up.
- **MenuSeparator**: 1px `track`, `sm` above and below, full content width.
- **MenuSection**: the section header (caption, bold, `textDim`, its 15% top pad), inset `controlPaddingX`, `sm` above, `xxs` below.

**States** (rows):

| State | Fill | Label / glyph |
|---|---|---|
| rest | none | `menu.text` |
| cursor (pointer, keyboard cursor or focus, `hasCursor`/`.cursor`) | `menu.selectedBackground`, small radius | `menu.selectedText` |
| pressed | `controls.pressed.fill` | as cursor |
| disabled | none, inert | `textFaint` (hint and marks too) |
| destructive | as above | `urgent` (stays urgent under the cursor) |

**Behavior.** Up/Down and k/j move the cursor over enabled rows and wrap; Home/End jump; Enter or Space runs the row; Right or l opens a submenu row's submenu, with its first row under the cursor; Left, h or Esc leave a submenu; Esc or a press outside closes the menu. Running a row closes the whole chain.

**Qt API.**

- `Menu` (a `BorderSurface`). Children are its rows.
  - Properties: `opened`, `held` (drawn open in place: no positioning, no dismissal), `trigger` (Item it opens below, `gap` = `xs` away), `minimumWidth` (`space(160)`), `currentIndex` (the cursor, an index into `rows`), and read-only `rows`, `currentItem`, `hasIcons`, `hasMarks`.
  - Functions: `open()` (below `trigger`), `popup(x, y)` (at a point in the parent, kept inside it), `close()`, `handleKey(event)` (for a panel's own key catcher).
  - Signals: `triggered(item)`, `closed()`.
  - Parent it to an item covering the window so it floats above the page.
- `MenuItem`.
  - Properties: `text`, `iconText`, `shortcut`, `checkable`, `checked`, `exclusive`, `destructive`, `enabled`, `submenu` (a Menu), `hasSubmenu` (defaults to `submenu !== null`), `hasCursor`, and read-only `highlighted` and `hot`.
  - Signal: `triggered()`. Function: `activate()`.
- `MenuSeparator {}`, `MenuSection { text }`.

**Rune API.**

- `Menu::new()`
  - Content: `.item(MenuItem)`, `.items(..)`, `.separator()`, `.section(label)`.
  - `.min_width(w)` (default `space(160)`), `.key(k)`, `.submenu(bool)`.
  - Placement: `.anchor(key)` (below a keyed trigger, `xs` away; push into the root overlay layer) or `.at(x, y)`. With neither, it draws in place, held open.
  - `.on_close(f)`: Escape, a press outside (modal while anchored or at a point), and Left/h in a submenu.
  - Keys: Up/Down/Home/End are the menu's focus group; j/k/l map to Down/Up/Right with or without `on_close` (through `Theme::key_map`, typed for the app's state by `Theme::apply`).
  - `.build(&theme)`.
- `MenuItem::new(label)`
  - Content: `.icon(glyph)`, `.shortcut(hint)`, `.check(bool)`, `.radio(bool)`, `.submenu(bool)`.
  - State: `.disabled(bool)`, `.destructive(bool)`, `.cursor(bool)`, `.key(k)`.
  - Handlers: `.on_press(f)` (press, Enter, Space) and `.on_open(f)` (Right/l, and a press when there is no `on_press`).

**Metrics.**

| Part | Value |
|---|---|
| Surface | `surfaces.menu` border (`max(1, space(2))` by default), padding `sm`, corners `radius` |
| Width | `max(space(160), widest row) + padding + border` |
| Row | `popupRowHeight` 28 tall, label inset `controlPaddingX` 10; width `2 × controlPaddingX + [icon + controlGap] + ⌈label⌉ + [huge + ⌈hint⌉] + [controlGap + icon]` |
| Separator | `2 × sm + 1` tall |
| Section | `sm` + header line (+ 15% pad) + `xxs` |

## ContextMenu

Qt `ContextMenu`; Rune `ContextMenu`. A Menu that opens at a point rather than under a trigger.

- **Qt**: everything Menu has, plus `target` (an Item where right-clicks open it at the pointer). `popup(x, y)` opens it at a point.
- **Rune**: the same builder as Menu (`item`, `items`, `separator`, `section`, `min_width`, `key`, `on_close`, `build`), with `.at(x, y)` in window points (from `on_secondary_press`'s `PointerPress::window_point()`). Without `at` it draws in place.

## Keycap and KeyCombo

Qt `Keycap`, `KeyCombo`; Rune `Keycap`, `KeyCombo` (plus `key_label`, `split_combo`, `keycap_width`, `key_combo_width`).

**Keycap** is one key: a `space(18)`-tall cap, small radius, `controls.normal` fill and border, `caption` text in the foreground centered by ink. Its width is `max(height, ⌈advance⌉ + 2 × sm + border)`.

Key names are normalized the same way in both bindings:

| Given (any case) | Drawn |
|---|---|
| super, meta, win, cmd, logo | `Super` |
| ctrl, control | `Ctrl` |
| alt, option | `Alt` |
| shift | `⇧` |
| enter, return | `⏎` |
| backspace | `⌫` |
| esc, escape | `Esc` |
| delete, del | `Del` |
| up, down, left, right | `↑ ↓ ← →` |
| pageup, pagedown | `PgUp`, `PgDn` |
| tab, space, home, end | `Tab`, `Space`, `Home`, `End` |
| one character | upper case |
| anything else | as given |

**KeyCombo** is a combination, "Ctrl+Shift+S", drawn as Keycaps `xxs` apart. A lone `+` is the plus key ("Ctrl++").

- **Qt**: `Keycap { key }` (read-only `label`, `foreground`); `KeyCombo { keys }`.
- **Rune**: `Keycap::new(key).build(&t)`, `KeyCombo::new(combo).build(&t)`.

## ShortcutSheet

Qt `ShortcutSheet`; Rune `ShortcutSheet`. Grouped keyboard shortcuts: the sheet's content, to put on a Card or in a dialog.

**Anatomy.** Groups fill `columns` columns (default 2), left to right in turn.

- Columns are `panelGap × 2` (28) apart.
- Groups in a column are `panelGap` apart.
- A group is a section header, then rows `md` apart (header to rows `md` too).
- A row is a KeyCombo in a column as wide as the group's widest combo, then `xxl` (12), then the description (`body`, foreground), both vertically centered.

**API.**

- **Qt**: `groups: [{ title, keys: [[combo, description], …] }]`, `columns`.
- **Rune**: `ShortcutSheet::new().group(title, [(combo, description), …]).columns(n).build(&t)`.

## Tooltip with shortcut

Qt `Tooltip` (the kit's own; `PanelToolTip` is unchanged); Rune `Tooltip` with `.shortcut(..)`.

**Anatomy.** The `surfaces.tooltip` surface (border `max(1, normal border width)`), padding `controlPaddingX` × `controlPaddingY` inside the border. It holds the text (`bodySmall`, tooltip text) and, when there is a shortcut, a KeyCombo `controlGap` after it, both vertically centered. Without a shortcut it is the plain Tooltip. Corners are the theme's `radius`, as every popup's: PanelToolTip, Button's built-in tooltip and this one all agree.

**API.**

- **Qt**: a `ToolTip` popup. Properties: `text`, `shortcut`, `held` (shown at the parent's top-left with no delay and no auto-close, for galleries), `delay` (400). It opens centered above its parent, `sm` away. Fades take 120 ms, and none under reduced motion.
- **Rune**: `Tooltip::new(text).shortcut("Ctrl+S").build(&t)`, drawn where it's placed; the app owns when it shows.

## Toolbar

Qt `Toolbar` and `ToolbarSeparator`; Rune `Toolbar` and `toolbar_separator`.

**Anatomy.**

- Three slots: leading at the left, center centered on the whole bar, trailing at the right.
- Items in a slot are `xxs` apart and vertically centered.
- `ToolbarSeparator` is a 1px `track` rule, `space(16)` tall, with `sm` clear each side; it splits groups.
- The optional overflow is an IconButton with U+F141 (…), after the trailing slot.
- The bar is `sm` inside its edges, with a 1px `track` rule along the bottom when `divided` (default on).

**API.**

- **Qt**: children go to `leading`; `center` and `trailing` are lists. Properties: `overflow`, `overflowHasCursor`, `divided`. Signal: `overflowTriggered()`.
- **Rune**: `Toolbar::new().leading([..]).center([..]).trailing([..]).overflow(bool).overflow_cursor(bool).divided(bool).on_overflow(f).build(&t)`.

## Tabs

Qt `Tabs`; Rune `Tabs`.

**Anatomy.**

- A row of text tabs `xxs` apart over a 1px `track` rule the full width of the control.
- Each tab is `⌈label⌉ + 2 × controlPaddingX` wide and `line(body) + 2 × controlPaddingY` tall, with small-radius corners.
- The selected tab's label is bold in `controls.selected.text`, with an `xxs`-tall accent underline across the tab at the bottom, over the rule. The underline slides between tabs over 160 ms on the ease-out curve, and doesn't move under reduced motion.

**States.**

| State | Fill | Label |
|---|---|---|
| rest | none | `textDim` |
| cursor (pointer, keyboard cursor or focus, `cursorIndex`/`.cursor`) | `controls.hover.fill` | foreground |
| pressed | `controls.pressed.fill` | foreground |
| selected | none (cursor fill when hot) | bold `controls.selected.text`, accent underline |

**Keyboard.** Left/Right or h/l move the selection, and Home/End jump. In Rune the tabs are a focus group, Left/Right move focus, and Enter or Space chooses.

**API.**

- **Qt**: `model` (strings), `currentIndex`, `cursorIndex`, signal `activated(index)`.
- **Rune**: `Tabs::new(labels, current).cursor(Option<usize>).on_choose(|s, i| ..).build(&t)`. Tabs are keyed `tab-<i>`.

## IconRail

Qt `IconRail`; Rune `IconRail`. A vertical tab rail for switching an app's side panels (after omareel's SidePanel).

**Anatomy.**

- Square items `controlHeight + 2 × sm` (36) on a side, `sm` apart, `sm` inside the rail.
- The rail is `item + 2 × sm + 1` wide, with a 1px `track` rule down its trailing edge.
- Glyphs are `iconLarge`, centered by ink. Items have `radius` corners.
- The selected item's glyph is the accent. An accent bar `xxs` wide and `iconLarge` tall (radius `min(1, radius)`) sits at the rail's leading edge, centered on the item, and slides over 160 ms.

**States.** Rest: no fill, glyph `textDim`. Cursor (pointer, keyboard cursor or focus): `controls.hover.fill`, glyph foreground. Selected: glyph accent, with the bar.

**Keyboard.** Up/Down or k/j move the selection, and Home/End jump. In Rune it is a focus group.

**API.**

- **Qt**: `model: [{ icon, label }]`, `currentIndex`, `cursorIndex`, `itemSize`, signal `activated(index)`.
- **Rune**: `IconRail::new([(glyph, label)], current).cursor(..).on_choose(..).build(&t)`. Items are keyed `rail-<i>`.

## Open questions

- No tokens were added. Candidates if these recur: a keycap height (`space(18)` here), a toolbar separator height (`space(16)`), and a menu minimum width (`space(160)`).

## Gallery

### `menus`

1. A row, `huge` apart, top-aligned:
   - **Menu**, held open:
     - section "File"
     - "New" (glyph U+F15B, "Ctrl+N")
     - "Open…" (U+F07B, "Ctrl+O", under the cursor)
     - "Save" (U+F0C7, "Ctrl+S")
     - "Duplicate" (U+F0C5, disabled)
     - separator
     - section "View"
     - "Sidebar" (checked)
     - "Word wrap" (checkable, unchecked)
     - separator
     - "Export" (U+F064, submenu chevron)
   - **ContextMenu**, held open:
     - "Cut" ("Ctrl+X"), "Copy" ("Ctrl+C"), "Paste" ("Ctrl+V")
     - separator
     - "By name" (radio, chosen), "By date" (radio)
     - separator
     - "Delete" ("Del", destructive)
   - **Tooltip** "Save" with the shortcut "Ctrl+S", held open.
2. A row of Keycaps, `lg` apart: Super, Ctrl, Alt, Shift, Tab, Esc, Enter, Backspace, Up, Space, K. Then a KeyCombo "Ctrl+Shift+P".
3. **ShortcutSheet**, 2 columns:
   - "File": Ctrl+N New file, Ctrl+O Open, Ctrl+S Save.
   - "View": Ctrl+B Sidebar, Ctrl+Shift+P Commands, F11 Full screen.

### `navigation`

1. **Toolbar**, full width:
   - Leading: three groups split by two ToolbarSeparators:
     - IconButtons U+F032, U+F033 (under the cursor), U+F0CD
     - U+F0CA, U+F0CB
     - U+F03E, U+F0C1
   - Center: a Heading "notes.md" (title level, centered).
   - Trailing: a bordered Button "Share", then the overflow.
2. **Tabs**, full width: General, Display, Audio, Network. Display is selected and Audio is under the cursor.
3. A row:
   - **IconRail** of 5 items (U+F07B Files, U+F002 Search, U+F126 Source control, U+F188 Debug, U+F013 Settings), Search selected.
   - A placeholder content box beside it, `space(320)` wide and as tall as the rail, with the selected item's name ("Search", `body`, `textDim`) centered on whole points.
