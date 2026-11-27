# Omarchy UI components

The primitives every binding provides, under the same names, with the same states and metrics. The Qt binding is the Omarchy shell's own kit (`qt/HARVEST.md`); the others are drawn to match it, and `bin/review` shows how closely. Token names refer to `spec/tokens.md`.

| Component | Qt (`import Omarchy.Ui`) | Rune (`omarchy_rune::ui`) | What it is |
|---|---|---|---|
| Button | `Button` | `Button` | Label, optional icon glyph. Borderless at rest unless `bordered`. `selected` fills and bolds. |
| IconButton | `IconButton` (`PanelActionButton`) | `IconButton` | Square, odd-sized, one glyph. |
| ButtonGroup | `ButtonGroup` | `ButtonGroup` | Bordered buttons `md` apart, one selected: a segmented choice. |
| ToggleSwitch | `ToggleSwitch` | `ToggleSwitch` | A bare switch; cursor ring around it. |
| Toggle | `Toggle` | `Toggle` | Labeled row (title, description) with the switch at the end; the whole row is the control. |
| TextField | `TextField` | `TextField` | Single-line input. |
| NumberField | `NumberField` | `NumberField` | Integer stepper with a label: [−] value [+]. |
| Dropdown | `Dropdown` | `Dropdown` | Labeled single choice; the list opens below the trigger on the popups surface. |
| Slider | `ValueSlider` (`PanelSlider`) | `Slider` | Thin track, foreground fill, knob ringed in the surface color, optional notches. |
| Card | `Card` | `Card` | The raised surface panels, popups and flyouts are drawn on. |
| SectionHeader | `SectionHeader` (`PanelSectionHeader`) | `section_header` | Caption, bold, `textDim`. |
| Separator | `Separator` (`PanelSeparator`) | `separator` | 1px foreground at 12%. |
| MenuRow | `MenuRow` (`PopupMenuRow`) | `MenuRow` | List/menu row: label, accent check when `checked`, hover fill when `highlighted`. |
| Tooltip | `PanelToolTip` | `Tooltip` | Tooltip surface, `bodySmall` text. |
| ConfirmDialog | `ConfirmDialog` | `ConfirmDialog` | Message, Cancel, destructive Confirm in urgent. |

Qt also carries the rest of the shell's kit (`SearchableDropdown`, `FilterInput`, `CursorSurface`, `PanelHero`, `OpticalGlyph`, `BorderSurface`, …). Rune also has all of rune-kit (Select, Menu, Popover, Table, Tree, Markdown, Diff, …), themed through `Theme::kit()`.

## The rest of the kit

Each group has its own file with every component's anatomy, API in both bindings, states, metrics and gallery specimens. The names are the same in Qt (`import Omarchy.Ui`) and Rune (`omarchy_rune::ui`).

| Group | Spec | Components | Gallery sections |
|---|---|---|---|
| Surfaces | [`components/surfaces.md`](components/surfaces.md) | Pane, Heading | `surfaces` |
| Lists | [`components/lists.md`](components/lists.md) | ListRow, CursorModel, ScrollArea, KeyValue, Hero, Badge, StatusDot, Avatar, TileGrid | `lists`, `tiles` |
| Feedback | [`components/feedback.md`](components/feedback.md) | Meter, ProgressBar, Spinner, EmptyState, Banner, Toast | `progress`, `messages` |
| Navigation | [`components/navigation.md`](components/navigation.md) | Menu, ContextMenu, Keycap, KeyCombo, ShortcutSheet, Tooltip (with shortcut), Toolbar, Tabs, IconRail | `menus`, `navigation` |
| Forms | [`components/forms.md`](components/forms.md) | SettingRow, Checkbox, Radio, RadioGroup, PasswordField, TextArea, MultiSelect, ColorWell, SwatchStrip, Dialog | `forms`, `pickers` |
| Data and layout | [`components/data.md`](components/data.md) | Table, Tree, SplitView, Sidebar, Calendar | `data`, `layout` |
| Editing | [`components/editing.md`](components/editing.md) | Timeline, Scrubber, TrimHandle, CropFrame, DragHandle, DropIndicator, ColorPicker | `timeline`, `color` |
| Composed | [`components/composed.md`](components/composed.md) | SearchList, and a Wi-Fi panel, search overlays and an app inspector built only from kit parts | `composed` |

Terminal bindings draw every component here on the cell grid as [`terminal.md`](terminal.md) maps it.

## Shared rules

- **Strings** a component shows by default (Cancel, Confirm, Search..., None selected, No matches for “%1”, Previous month) come from one catalog in both bindings: `qt/qml/Omarchy/Theme/Strings.js`, harvested from Omarchy's `default/i18n` for every key a Qt kit file asks for (`I18n.t`), read by Rust through `omarchy_theme::strings` (`Tokens::string`). Both follow `~/.config/omarchy/language`. A component that adds one asks for it with `I18n.t(key, english)` in Qt and `tk.string(key, english)` in Rune, then `bin/harvest-qt` carries it.

- **States** follow `[controls]`: pressed > focus > hover-cursor > selected > normal. Focus looks like hover unless the theme says otherwise. Every component takes a `cursor` / `hasCursor` flag that forces the hover-cursor look, for keyboard-driven lists and for still renders.
- **No relayout on state change.** A component reserves the widest border any of its states paints.
- **Corners** are `radius` (Hyprland's rounding, usually 0). Small parts use `min(4, radius)`. Switches and slider knobs are the only round things, and switches only when `radius > 0`.
- **Color** is foreground glass: fills and borders are the foreground at the state's alpha. Accent marks only what is *chosen* (a menu's selected text, a check, the active border on cards). Urgent marks destructive actions.
- **Type** is one family (fontconfig `monospace`) at the token sizes; selection is shown with bold, nothing else changes weight.
- **Motion** is 120 ms for color, 160 ms for the press give (scale 0.97), zero under reduced motion.

## Metrics (at scale 1)

| Part | Value |
|---|---|
| Button padding | `controlPaddingX` 10 × `controlPaddingY` 6, icon–label gap `controlGap` 8, label `body`, height from `body` line box (icon + label matches text-only height) |
| IconButton | `max(space(22), icon + sm × 2) | 1` square, glyph `icon` optically centered horizontally by ink bounds and vertically on the line box, corners `min(4, radius)` |
| ButtonGroup gap | `md` 6 |
| ToggleSwitch | track `max(22, round(controlHeight × 0.55))` tall, × 1.9 wide; knob `round(track × 0.72)`; cursor ring `space(6)` outside; knob off = `textDim`, on = selected text |
| Toggle | min height 54, padding `rowPaddingX` 12, title `subtitle` bold, description `caption` `textDim` |
| TextField | padding `controlPaddingX` × `inputPaddingY` 7, text `body`, placeholder `textFaint` |
| NumberField | `numberFieldWidth` 120 × `controlHeight` 28, label `caption` bold `textDim`, `labelGap` above (matches Dropdown) |
| Dropdown | `dropdownWidth` 240 × `controlHeight`, label `caption` bold `textDim`, chevron `textDim` |
| Slider | track `max(4, round(controlHeight × 0.11))`, knob `max(14, round(controlHeight × 0.38))` with a `space(2)` transparent gap split out of the track, interior notches `space(2)` wide |
| Card | `panelPadding` 18, children `panelGap` 14 apart, popups border at `max(1, space(2))` |
| MenuRow | `popupRowHeight` 28, text inset `controlPaddingX` |
| Tooltip | padding `controlPaddingX` × `controlPaddingY`, border `max(1, normal border width)`, corners `radius` (Button's built-in tooltip too: `qt/patches/0004`) |
| ConfirmDialog | `space(370)` wide, `space(18)` padding, message `title`, buttons min `space(88)` × `space(34)` (`controlPaddingX` each side), `space(10)` apart, labels `caption`, corners `radius` |
