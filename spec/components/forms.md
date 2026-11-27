# Forms and pickers

Setting rows, check and radio rows, secret and multi-line inputs, a multi-choice dropdown, color wells and a modal dialog. Token names refer to `spec/tokens.md`; "fg" is `palette.foreground`, "hover", "normal" and "selected" are the `[controls]` states.

| Component | Qt (`import Omarchy.Ui`) | Rune (`omarchy_rune::ui`) | Source |
|---|---|---|---|
| SettingRow | `SettingRow` | `SettingRow` | own (port of omareel `OmSettingRow`) |
| Checkbox | `Checkbox` | `Checkbox` | own |
| Radio | `Radio` | `Radio::new` (a `Checkbox` with `exclusive`) | own |
| RadioGroup | `RadioGroup` | `RadioGroup` | own |
| PasswordField | `PasswordField` | `PasswordField` | own, on the harvested `TextField` (lock and polkit are the references) |
| TextArea | `TextArea` | `TextArea` | own |
| MultiSelect | `MultiSelect` | `MultiSelect` | harvested from `shell/Ui/MultiSelect.qml`, plus `qt/patches/0002-multiselect-plain-model.patch` |
| ColorWell | `ColorWell` | `ColorWell` | own (port of omareel `OmColorWell`) |
| SwatchStrip | `SwatchStrip` | `SwatchStrip` | own |
| Dialog | `Dialog` | `Dialog` | own; ConfirmDialog is its preset |

## SettingRow

A row of a settings list: words on the left, any control on the right. Stacked rows line their labels and controls up.

- **Anatomy**: label; optional description under it; the control, right-aligned and vertically centered.
- **Qt**: `label`, `description`, `hasCursor`, `foreground`; the control is the default child. `enabled: false` dims the words.
- **Rune**: `SettingRow::new(label).description(d).control(el).cursor(b).disabled(b).width(w).build(&t)`.
- **States**: normal (no chrome); cursor = `hover.fill` across the row; disabled = words in `textFaint`. The control keeps its own states and keyboard.
- **Metrics**: padding `rowPaddingX` × `md`; height `max(controlHeight + 2 × md, words + 2 × md, trailing + 2 × xxs)` so controls with outer cursor rings (`ToggleSwitch`, `ColorWell`) stay aligned with sibling rows; label `body` fg, description `caption` `textDim`, `xxs` apart; words to control `xxl`; corners `radius`.

## Checkbox, Radio, RadioGroup

- **Anatomy**: a box (`radius_small` square, or a circle for Radio), then a label and optional description. Checked: Checkbox shows a bold `✓` in accent, Radio an accent dot.
- **Qt Checkbox / Radio**: `label`, `description`, `checked`, `exclusive` (Radio sets it), `hasCursor`, `foreground`, `accent`, `enabled`; signals `toggled()`, `hovered(bool)`. The caller owns `checked` (flip it on `toggled()`, as ToggleSwitch). Space and Return toggle while focused; Tab focuses.
- **Qt RadioGroup**: `options` (string[] or `{ value, label, description? }`), `value`, `cursorIndex`; signal `changed(value)`; Up/Down/Left/Right and j/k move the choice.
- **Rune**: `Checkbox::new(label).description(d).checked(b).cursor(b).disabled(b).key(k).on_toggle(f)`; `Radio::new(label)` is the same with `exclusive(true)`; `RadioGroup::new([(value, label)], value).description(value, d).cursor(Some(i)).on_choose(f)` (a `focus_group`, so arrows rove).
- **States** (on the box; priority hover > selected > normal): normal = `normal.fill` + `normal.border`; checked = `selected.fill` + `selected.border` + accent mark; cursor / pointer / focus = `hover.fill` + `hover.border` (the mark stays); disabled = box at opacity 0.5, words `textFaint`, no hover.
- **Metrics**: box `space(16)`; check `round(box × 0.85)` bold; dot `round(box / 2)`; box to words `rowGap`; label `body` fg, description `caption` `textDim`, `xxs` apart; the box and the label line center on each other (`round((line − box) / 2)`); RadioGroup rows `lg` apart.

## PasswordField

A TextField for secrets.

- **Anatomy**: the TextField chrome; a lock glyph `` (U+F023) before the text; dots (`•`) for the text; optional eye `󰈈` / `󰈉` (U+F0208 / U+F0209) at the end; with an error, the message under the field.
- **Qt**: `text`, `placeholderText`, `revealable`, `revealed`, `error` (non-empty = error state), `hasCursor`, `field` (the TextField); signal `accepted()`; `shake()` replays the error animation. Setting `error` runs it.
- **Rune**: `PasswordField::new(&edit).placeholder(p).width(w).cursor(b).revealed(b).on_reveal(f).revealable(b).error(msg).shaker(&live).on_edit(f).build(&t)`; `PasswordField::shake(&live, &t)` pulses a bound `Live<[f32; 2]>`.
- **States**: TextField's (normal border at rest; hover/focus/cursor = hover look). Error: border `surfaces.polkit.borderError` at `max(1, normal width)`, lock and message in `polkit.textError`. Motion: polkit's shake (−8, +8, 0 over 35/50/55 ms, `space(8)`) and an urgent flash (`selected` fill alpha fading over 400 ms) in Qt, a spring pulse in Rune; both land at once under `reduceMotion`, so a still shows only the colors.
- **Metrics**: TextField's padding (`controlPaddingX` × `inputPaddingY`) and height; placeholder `textFaint` (TextField's darker 1.6 would outweigh the text in a light theme); glyphs `body` in `textDim`; glyph to text `controlGap`; message `caption`, `labelGap` below.

## TextArea

- **Anatomy**: TextField's chrome around wrapped multi-line text; a thin scrollbar (`scrollbarThickness`, `textFaint`) only when the text overflows (Qt).
- **Qt**: `text`, `placeholderText`, `minRows` (3), `maxRows` (6), `hasCursor`, `area` (the QQC TextArea).
- **Rune**: `TextArea::new(&edit).placeholder(p).rows(min, max).width(w).cursor(b).on_edit(f).build(&t)` (on `rune::auto_grow`).
- **States**: as TextField.
- **Metrics**: padding `controlPaddingX` × `inputPaddingY` (+ border); text `body`, placeholder `textFaint`; height `clamp(lines, minRows, maxRows) × line + padding`, where line is the `body` line box (16 at 12 px).

## MultiSelect

The shell's searchable multi-choice dropdown. The patch drops `optionsCommand` (a Quickshell `Process`): options are a plain model; an app that loads them sets `loadingOptions` / `optionsError` itself and sets `refreshable` to show the refresh button, which emits `refreshRequested()`. `bin/harvest-qt` now carries `common.noOptions`, `common.noneSelected`, `common.loading` and every `multiSelect.*` string for every language.

- **Anatomy**: label; trigger (the chosen labels joined with ", ", or "N selected" past three, or "None selected" dim; chevron `󰅀`); popup below it on the popups surface: a search field, a rule, then a checkbox row per option.
- **Qt**: `label`, `values`, `options` (string[] or `{ value, label, description? }`), `placeholderText`, `emptyText`, `noSelectionText`, `triggerLabel`, `showLabel`, `hasCursor`, `refreshable`, `loadingOptions`, `optionsError`; `open()`, `close()`, `toggle()`, `popupOpen`; signals `changed(values)`, `hovered(bool)`, `refreshRequested()`.
- **Rune**: `MultiSelect::new(key, [(value, label)], values).label(l).open(b).cursor(b).highlighted(Some(i)).width(w).search(&edit).on_search(f).texts(placeholder, none, empty).on_toggle(f).on_choose(f).build(&t, layer)`; the popup goes in the overlay `layer`, anchored to `key`. Rows are keyed `<key>-<value>`.
- **Keys** (both): on the trigger j, Down, Return or Space opens it; in the search typing filters, Return toggles the first match and Down goes into the list; in the list j/k and Down/Up walk (stopping at the last row; Up off the first goes back to the search), Return or Space toggles; Escape closes (clearing the search). Qt moves focus into the popup. Rune keeps focus on the trigger and the app owns the state: `.on_state(|s| (&mut s.open, &mut s.search, &mut s.row))` (instead of `on_toggle` and `on_search`), passing the same values to `.open`, `.search` and `.highlighted` (the row among those the search shows, `None` while the keys are in the search); typing on the trigger lands in the search.
- **Strings**: the search placeholder, "None selected", "No options" and "N selected" come from the catalog (`common.search`, `common.noneSelected`, `common.noOptions`, `multiSelect.selectedCount`) in both; `.texts(..)` overrides them in Rune.
- **States**: trigger as Dropdown; the search field wears the focus look while open (it holds focus); rows show the hover fill under the cursor; a chosen row's box is `selected.fill` + `selected.border` with a bold `✓` in `selected.text`.
- **Metrics**: width `searchableDropdownWidth`; label `caption` bold `textDim`, `labelGap` above the trigger; trigger `controlHeight`, text inset `controlPaddingX`, chevron `textDim` inset `controlGap`. Popup `xxs` below, the trigger's width, border = popups border at the normal width, padding border + `hairline`, height `min(rows, 6 rows) + search header + rule + vertical padding`; search header `popupRowHeight + controlPaddingX` with `md` margins; rule 1 px fg at 0.10; rows `popupRowHeight`, `labelGap` apart, corners `radius_small`, inset `controlPaddingX`, box `space(16)` with corners `radius_small`, box to label `rowGap`.

## ColorWell

- **Anatomy**: a bordered well with the color inset in it; a cursor ring outside (as ToggleSwitch draws its cursor, since a bordered well would only get fainter under the hover border).
- **Qt**: `swatch`, `hasCursor`; signals `clicked()`, `hovered(bool)`; Space/Return press. Pressing gives (scale 0.97, `press` motion).
- **Rune**: `ColorWell::new(color).cursor(b).on_press(f).build(&t)`.
- **States**: well = `normal.fill` + `normal.border` always; cursor / pointer / focus add the ring in `hover.border`.
- **Metrics**: well `space(44)` × `space(26)`, corners `radius_small`; color inset border + `xxs`, corners `max(0, radius_small − inset)`; ring `sm` outside the well, corners `min(radius, radius_small + sm)` (0 when sharp).

## SwatchStrip

- **Anatomy**: a row of color chips, each with a 1 px `track` rim (so a background-colored chip shows); a ring around the chosen one.
- **Qt**: `colors`, `currentIndex`, `cursorIndex`; signal `picked(index)`; Left/Right (h/l) move the cursor, Space/Return pick.
- **Rune**: `SwatchStrip::new(colors, Some(i)).cursor(Some(i)).on_pick(f).build(&t)` (a `focus_group`).
- **States**: chosen = ring in accent; cursor / pointer = ring in `hover.border` color; chosen wins.
- **Metrics**: chip `space(22)`, corners `radius_small`; ring `max(1, space(2))` wide, `xxs` outside the chip, corners `radius_small + xxs + ring` (0 when sharp); cells `xxs` apart.

## Dialog

A modal card on the `scrim`. ConfirmDialog is this card with no title, the message in `title` size and a destructive Confirm (in Rune, `ConfirmDialog::build` builds a `Dialog`; the harvested Qt ConfirmDialog is left as the shell has it, drawing the same card).

- **Anatomy**: scrim filling the host; centered card: title, message, body (any children), then a button row at the bottom right: secondary, primary.
- **Qt**: `opened`, `title`, `message`, body = default children, `secondaryText`, `primaryText` (Cancel / Confirm, translated), `destructive`, `selectedIndex` (0 secondary, 1 primary), colors; signals `rejected()`, `accepted()`; `handleKey(event)`. Escape rejects, Return takes the selected button, Left/Right/h/l/Tab move the selection; a press on the scrim rejects.
- **Rune**: `Dialog::new(title).message(m).body(el).labels(secondary, primary)` (`common.cancel` / `common.confirm` by default, translated, as Qt's; ConfirmDialog the same) `.destructive(b).selected(i).width(w).scrim(b).modal(b).on_cancel(f).on_accept(f).build(&t)`. The selected button is the window's default button (Return); `modal(true)` traps focus, makes the rest inert and routes Escape and outside presses to `on_cancel`.
- **States**: the selected button: `menu.selectedBackground` fill with `menu.selectedText` border and label, or for a destructive primary urgent at 0.22 with urgent border and label. Unselected: transparent, border fg at 0.38 (urgent at 0.56 when destructive), label fg.
- **Metrics**: card `min(host − space(32), space(370))` wide, padding `space(18)`, border `menu.selectedText` at the normal width, corners `radius`, background `palette.background`; title `title` bold; message `body`; title, message and body `space(10)` apart; buttons `space(20)` below, min `space(88)` × `space(34)` (`controlPaddingX` each side), `space(10)` apart, label `caption`, corners `radius`.

## Gallery

| Section | Specimens |
|---|---|
| `forms` | A 480-wide column of SettingRows: "Night light" / "Warm the screen after sunset" with a ToggleSwitch on; "Theme" / "Applies to every app" with a 180-wide Dropdown (no label) = Tokyo Night; "Gaps" / "Space between windows" with a NumberField (no label) 10 (0–40). Then, `huge × 2` apart and top-aligned: a column of Checkboxes `lg` apart — "Show seconds" unchecked, "Show date" checked, "Show weekday" cursor, "Show week number" disabled, "Twelve-hour clock" / "Show AM and PM" checked — and a RadioGroup Top / Bottom / Hidden with Bottom chosen. Then three 220-wide PasswordFields `controlGap` apart, top-aligned: empty with placeholder "Password"; "hunter22" (dots) with the reveal eye; "letmein" (dots) with error "Wrong password, try again". Then a 480-wide TextArea holding "Back up your dotfiles" / "before switching themes," / "then run omarchy update." (3 lines). |
| `pickers` | MultiSelect "Bar modules" (Clock, Battery, Network, Volume, Bluetooth) closed with the first four chosen, reading "4 selected"; `xxl` beside it, in a 280-tall box, MultiSelect "Notify for" (Calendar, Mail, Messages, Updates, Battery) held open with Mail and Updates checked, reading "Mail, Updates". Then ColorWells `controlGap` apart: accent, success with cursor, urgent. Then a SwatchStrip of accent, urgent, warning, success, foreground, textDim, textFaint, muted with warning (index 2) chosen. Then a 420 × 240 box: a Card "Displays" filling it (MenuRows "Built-in display" checked, "External monitor", "Projector"), and over it a Dialog with the scrim: "Discard changes?" / "Your edits to Tokyo Night will be lost.", buttons "Keep editing" and "Discard", destructive, Discard selected. |
