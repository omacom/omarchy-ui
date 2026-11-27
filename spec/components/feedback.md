# Feedback components

Meter, ProgressBar, Spinner, EmptyState, Banner and Toast: what a view shows about levels, work in progress, emptiness and outcomes. Roadmap Tier 1 #4, #5, #6 and Tier 2 #21. Qt files are `qt/qml/Omarchy/Ui/<Name>.qml`; Rune builders are in `rune/omarchy-rune/src/ui/feedback.rs` (re-exported from `omarchy_rune::ui`, with `Tone` at `omarchy_rune::ui::feedback::Tone`).

## Tone

Meter, ProgressBar and Banner share one set of tones. Qt takes a string; Rune takes `feedback::Tone`.

| Qt `tone` | Rune | Color |
|---|---|---|
| `"neutral"` | `Tone::Neutral` | `foreground` |
| `"info"` | `Tone::Info` | `accent` |
| `"success"` | `Tone::Success` | `success` |
| `"warning"` | `Tone::Warning` | `warning` |
| `"error"` | `Tone::Error` | `urgent` |

## Meter

Qt `Meter`, Rune `Meter`. A determinate level on a track: battery, volume, usage against a limit. It is the bar the shell draws by hand in the OSD (`osd/Osd.qml`), the power panel and the agents panel (`component Meter`, with markers and `alarming`).

**Anatomy.** An optional header row (label at the start, value text at the end), then the bar: a `track`, a fill from the leading edge, and optional marker ticks across the track.

**API.**

| Qt | Rune | |
|---|---|---|
| `value: real` (0–1) | `Meter::new(value)` | clamped to 0–1 |
| `tone: string` | `.tone(Tone)` | fill color, `"neutral"` by default |
| `alarming: bool` | `.alarming(bool)` | fill turns `urgent` whatever the tone |
| `markers: list<real>` | `.markers(impl IntoIterator<f32>)` | ticks at these fractions |
| `label: string` | `.label(s)` | header, start |
| `valueText: string` | `.value_text(s)` | header, end |
| `width` / `Layout.preferredWidth` | `.width(w)` | fills its container's width by default in Rune; `space(200)` implicit in Qt |
| `thickness: real` | | `meterThickness` |

Accessible role progress bar, with the label as its name; Rune reports the value as a range.

**States.** Normal; alarming. No interaction. The fill's width animates 160 ms out-cubic in Qt (0 under reduced motion).

**Metrics.**

| Part | Value |
|---|---|
| Track | `meterThickness` (6) tall, `track` color, radius `min(radiusSmall, thickness / 2)`: sharp at radius 0, a pill at radius ≥ 4 |
| Fill | tone color (urgent when alarming), same height and radius |
| Marker tick | `max(2, round(thickness × 0.5))` wide (3), `round(thickness × 2.5)` tall (15), radius `min(radiusSmall, width / 2)`, the fill's color, centered on the track; x = `fraction × (trackWidth − tickWidth)`, so the first and last sit inside the ends |
| Bar height | `thickness`, or the tick height when there are markers (track centered in it) |
| Header | label `body` `foreground`, value `body` `textDim`, `md` (6) above the bar |

## ProgressBar

Qt `ProgressBar`, Rune `ProgressBar`. Progress of a task: a Meter with an `info` (accent) fill by default, as the OSD and the apps' export bars draw it, plus an indeterminate mode. Qt's is `Meter { tone: "info" }`; Rune's wraps a `Meter`.

**API.** Everything Meter has except markers and alarming in Rune (Qt inherits them all), plus:

| Qt | Rune | |
|---|---|---|
| `indeterminate: bool` | `ProgressBar::indeterminate()` | work of unknown length |
| `value` | `ProgressBar::new(value)` | |

**States.** Determinate; indeterminate. Indeterminate draws a segment a quarter of the track wide that sweeps from just off the start to just off the end every 1.4 s (ease in-out), clipped to the track, and wraps. Under reduced motion it holds still in the middle: x = 3/8 of the track.

**Metrics.** As Meter. Segment `round(trackWidth × 0.25)` wide.

## Spinner

Qt `Spinner`, Rune `Spinner`. Busy: a Nerd Font glyph turning once every 900 ms (the shell spins `󰦖` at 800–900 ms in the weather panel, MultiSelect and Button's `iconSpinning`), or with `pulse` a glyph breathing between full and half opacity, 420 ms each way (the Tailscale panel's account switch).

**Anatomy.** One glyph, its ink centered horizontally in a square box the size of the glyph's line height, so it turns about its own middle.

**API.**

| Qt | Rune | |
|---|---|---|
| `glyph: string` | `.glyph(s)` | `\u{f0996}` (`󰦖`) by default |
| `size: real` | `.size(px)` | a font token; `icon` (14) by default |
| `pulse: bool` | `.pulse(bool)` | breathe instead of turn |
| `color: color` | `.color(Color)` | `foreground` by default |
| `running: bool` | | Qt only: stop the animation without hiding |

**States.** Turning; pulsing; still. A turn is linear, clockwise, about the box's middle. A pulse starts at 0.5, rises to 1 and falls back, 420 ms each way, eased in and out. Under reduced motion the glyph stands upright at full opacity, and a pulse rests at 0.5 opacity.

Qt turns the `Text` (a `RotationAnimator`). Rune repeats a turn (`LoopSpec::rotation`) only for an element's icon, so its turning spinner draws the glyph's own outline (read with swash from the font fontconfig picks for it) as an icon, its ink where the text would put it, and turns that; still, or when no font has an outline for the glyph, it is the text. `Spinner::TURN` (0.9 s) and `Spinner::PULSE` (0.84 s) are the periods.

**Metrics.** Glyph at `size`; box `lineHeight(size)` square.

## EmptyState

Qt `EmptyState`, Rune `EmptyState`. What a list or panel shows when it has nothing (about 14 hand-built in the shell). Ported from omareel's `OmEmptyState` onto the kit's tokens.

**Anatomy.** A centered column in the width it is given: glyph, title, optional hint, optional action Button (bordered). Title and hint wrap, centered.

**API.**

| Qt | Rune | |
|---|---|---|
| `title: string` | `EmptyState::new(title)` | |
| `glyph: string` | `.glyph(s)` | |
| `hint: string` | `.hint(s)` | |
| `actionText: string` | `.action(label)` | |
| `signal actionClicked()` | `.on_action(f)` | |
| `size: "panel" \| "overlay"` | `.overlay(bool)` | `panel` by default |
| `hasCursor: bool` | `.cursor(bool)` | forces the action's hover-cursor look |

**States.** Panel; overlay; action rest / hover-cursor (Button's).

**Metrics.**

| Part | `panel` | `overlay` |
|---|---|---|
| Glyph | `iconLarge` (18), `textDim` | `displayLarge` (28), `menu.selectedText`, opacity 0.8 (clipboard, emojis) |
| Title | `body`, `textDim` | `title`, `foreground`, opacity 0.7 |
| Hint | `bodySmall`, `textFaint` | `body`, `textDim` |
| Gap | `sm` (4) | `lg` (8) |
| Action | Button, bordered | Button, bordered |

## Banner

Qt `Banner`, Rune `Banner`. An inline message in the page: info, success, warning or error. Flat: a foreground-glass fill with a border in the tone, never a filled saturated background.

**Anatomy.** A row: tone glyph, text (wraps, takes the slack), optional action Button (bordered), optional dismiss IconButton (`󰅖`).

**API.**

| Qt | Rune | |
|---|---|---|
| `text: string` | `Banner::new(text)` | |
| `tone: string` | `.tone(Tone)` | `info` by default; `neutral` draws as `info` |
| `glyph: string` | `.glyph(s)` | replaces the tone's glyph |
| `actionText: string` | `.action(label)` | |
| `dismissible: bool` | `.dismissible(bool)` | |
| `signal actionClicked()` | `.on_action(f)` | |
| `signal dismissed()` | `.on_dismiss(f)` | |
| `hasCursor: bool` | `.cursor(bool)` | forces the action's hover-cursor look |

Tone glyphs: info `󰋽` (U+F02FD), success `󰄬` (U+F012C), warning `󰀪` (U+F002A), error `󰅚` (U+F015A). Accessible role alert.

**States.** Four tones; with or without action and dismiss; the action and dismiss have the Button / IconButton states.

**Metrics.**

| Part | Value |
|---|---|
| Fill | `controls.normal.fill` (foreground × 0.04) |
| Border | `max(1, normal border width)`, the tone color |
| Corners | `radius` |
| Padding | `controlPaddingX` (10) start; end `sm` (4) with an action or dismiss, else `controlPaddingX`; `sm` top and bottom |
| Min height | `max(controlHeight, bordered button height)` (30 at scale 1) inside the padding so action and dismiss-only banners match |
| Glyph | `icon` (14), tone color |
| Text | `body`, `foreground` |
| Gaps | `controlGap` (8) |

## Toast

Qt `Toast`, Rune `Toast`. A transient message inside the app, drawn on the `notifications` surface the shell's own toasts use. It draws one state: the app owns when it shows, where it stacks and the timer behind `countdown`. Messages for the desktop, not the app, go through `omarchy-notification-send` instead.

**Anatomy.** The surface (background, border spec, Hyprland radius); a row of optional glyph, a column of bold title and text, and an optional action Button (bordered); a countdown line along the bottom inside the border.

**API.**

| Qt | Rune | |
|---|---|---|
| `title: string` | `Toast::new(title)` | |
| `text: string` | `.text(s)` | wraps |
| `glyph: string` | `.glyph(s)` | |
| `actionText: string` | `.action(label)` | |
| `signal actionClicked()` | `.on_action(f)` | |
| `countdown: real` | `.countdown(f32)` | time left, 1 → 0; below 0 (Qt's default) or unset hides the line |
| `hasCursor: bool` | `.cursor(bool)` | the keyboard's toast: tinted with the hover fill, as the shell tints its selected card |
| `width` | `.width(w)` | `space(360)` by default |

**States.** Rest; cursor (hover-fill tint inside the border); with or without countdown.

**Metrics.**

| Part | Value |
|---|---|
| Surface | `notifications.background`, `notifications.border` spec at `max(1, space(2))`, `radius` |
| Padding | `space(12)` × `space(10)` inside the border |
| Glyph | `iconLarge` (18), `notifications.text`, `space(12)` before the text |
| Title | `body` bold, `notifications.text` |
| Text | `body`, `notifications.text` at 78% opacity (dims cleanly on dark and light themes), `space(3)` under the title |
| Action | Button, bordered, `space(12)` after the text |
| Countdown | `max(1, space(2))` tall along the inner bottom edge, inset `radius` from each side, `notifications.countdown`, `countdown ×` that width from the leading edge |

## Gallery

Two sections, after `surfaces`.

| Section | Specimens |
|---|---|
| `progress` | A row `huge` apart of three Meters 220 wide, markers at 0.5 and 0.8: 0.2; 0.65; 0.92 alarming. Meter 0.65 labeled "Battery" / "65%", 360 wide. A row `huge` apart of two ProgressBars 340 wide: determinate 0.4; indeterminate (still, mid-track). A row `huge` apart of four Spinners: glyph at `icon`; glyph at `iconLarge`; pulse at `icon`; pulse at `iconLarge`. |
| `messages` | A row `huge` apart of two 350 × 150 boxes, each with an EmptyState centered at the box's width: panel, `󰖪` "No networks found" / "Turn on Wi-Fi to scan", action "Scan"; overlay, `󰅌` "Clipboard is empty" / "Copy something to see it here". Four dismissible Banners at full width: info "Omarchy 3.2 is available" with action "Update"; success "Backup finished"; warning "Battery is low (12%)"; error "Could not reach the update server". A Toast held open, 360 wide: `󰄬` "Export finished" / "omarchy-demo.mp4 · 42 MB", action "Open", countdown 0.6. |
