"""Surfaces (spec/terminal.md, Surfaces): boxes with a light border in the surface's border color,
corners by the radius, padding 2 columns × 1 row; and the in-window Pane, Heading, SectionHeader,
Separator, MenuRow, Tooltip and the dialogs. A port of `omarchy-ratatui`'s `ui/surfaces.rs`, cell
for cell."""

from __future__ import annotations

import enum
from typing import Iterable, Sequence

from ..color import TRANSPARENT
from . import paint
from .base import Draw, Look, glyphs
from .buffer import Buffer, Rect
from .controls import control, wrap
from .navigation import KeyCombo
from .paint import Ink, clear, fill, frame, hline, put, text_center, text_fit, vline, width

__all__ = [
    "Card",
    "ConfirmDialog",
    "Dialog",
    "Heading",
    "HeadingLevel",
    "MenuRow",
    "Pane",
    "SectionHeader",
    "Separator",
    "Side",
    "Tooltip",
    "section_header",
]


def _stack_size(t, kids: Sequence[Draw], gap: int) -> tuple[int, int]:
    w = h = 0
    for i, k in enumerate(kids):
        cw, ch = k.size(t)
        w = max(w, cw)
        h += ch + (gap if i > 0 else 0)
    return (w, h)


def _stack_draw(t, kids: Sequence[Draw], gap: int, area: Rect, buf: Buffer) -> None:
    y = area.y
    for k in kids:
        cw, ch = k.size(t)
        r = Rect(area.x, y, min(cw, area.width), ch).intersection(area)
        if r.height > 0 and r.width > 0:
            k.draw(t, r, buf)
        y += ch + gap


class SectionHeader(Draw):
    """The caption that labels a group of rows: bold, `textDim`."""

    def __init__(self, text: str):
        self.text = text

    def size(self, t) -> tuple[int, int]:
        return (width(self.text), 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        text_fit(t, buf, area.x, area.y, self.text, area.width, Ink(t.dim(), bold=True))


def section_header(text: str) -> SectionHeader:
    return SectionHeader(text)


class Separator(Draw):
    """A rule of `─` in `track` across its width."""

    def __init__(self, w: int):
        self.w = w

    def size(self, t) -> tuple[int, int]:
        return (self.w, 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        hline(t, buf, area.x, area.y, area.width, "─", t.track())


class Card(Draw):
    """The raised surface: a box in the popups border color, its title a SectionHeader in the top
    edge, children one row apart inside a 2-column, 1-row padding."""

    def __init__(self, *children: Draw, title: str = "", width: int = 45):
        self.title, self.width = title, width
        self.children: list[Draw] = list(children)

    def child(self, d: Draw) -> "Card":
        self.children.append(d)
        return self

    def size(self, t) -> tuple[int, int]:
        return (self.width, _stack_size(t, self.children, 1)[1] + 4)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        clear(buf, area)
        s = t.shown.surfaces.popups
        title = (self.title, Ink(t.dim(), bold=True)) if self.title else None
        inner = frame(t, buf, area, s.border.color, title)
        _stack_draw(t, self.children, 1, paint.inset(inner, 2, 1), buf)


class Side(enum.Enum):
    """Which side a Pane's rule is on."""

    LEFT = "left"
    TOP = "top"
    RIGHT = "right"
    BOTTOM = "bottom"


class _Head(Draw):
    """A Heading over borrowed parts (a Pane's header lends its actions)."""

    def __init__(self, text: str, subtitle: str, centered: bool, trailing: Sequence[Draw], width: int | None):
        self.text, self.subtitle, self.centered, self.trailing, self.width = text, subtitle, centered, trailing, width

    def trailing_w(self, t) -> int:
        n = len(self.trailing)
        return sum(d.size(t)[0] for d in self.trailing) + max(0, n - 1) + (1 if n > 0 else 0)

    def size(self, t) -> tuple[int, int]:
        tw = self.trailing_w(t)
        trail_h = max((d.size(t)[1] for d in self.trailing), default=0)
        lines_h = 1 if not self.subtitle else 2
        h = max(lines_h, trail_h)
        if trail_h > 1 or self.centered:
            natural = max(width(self.text), width(self.subtitle)) + tw
        else:
            natural = max(width(self.text) + tw, width(self.subtitle))
        return (self.width if self.width is not None else natural, h)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        tw = self.trailing_w(t)
        lines_h = 1 if not self.subtitle else 2
        title_w = max(0, area.width - tw)
        trail_on_sub = any(((max(lines_h, h) - h) // 2) + h > 1 for _, h in (d.size(t) for d in self.trailing))
        sub_w = title_w if (self.centered or trail_on_sub) else area.width

        def put_line(y: int, w: int, s: str, i: Ink) -> None:
            if self.centered:
                text_center(t, buf, area.x, y, w, s, i)
            else:
                text_fit(t, buf, area.x, y, s, w, i)

        put_line(area.y, title_w, self.text, Ink(t.fg(), bold=True))
        if self.subtitle:
            put_line(area.y + 1, sub_w, self.subtitle, Ink(t.dim()))
        x = max(0, area.right - tw) + (1 if tw > 0 else 0)
        for d in self.trailing:
            w, h = d.size(t)
            dy = (max(lines_h, h) - h) // 2
            d.draw(t, Rect(x, area.y + dy, w, h).intersection(area), buf)
            x += w + 1


class Pane(Draw):
    """A flat surface inside an app window: padding, an optional Heading with actions, children in
    a column one row apart. Plain (nothing drawn), divided (a `track` rule on one side) or bordered
    (the controls' normal glass in a box at the radius)."""

    def __init__(self, *children: Draw, title: str = "", subtitle: str = "", actions: Iterable[Draw] = (),
                 divider: Side | None = None, bordered: bool = False, width: int = 30):
        self.title, self.subtitle = title, subtitle
        self.actions: list[Draw] = list(actions)
        self.divider, self.bordered, self.width = divider, bordered, width
        self.children: list[Draw] = list(children)

    def child(self, d: Draw) -> "Pane":
        self.children.append(d)
        return self

    def action(self, d: Draw) -> "Pane":
        self.actions.append(d)
        return self

    def edge(self) -> tuple[int, int, int, int]:
        """Extra cells on (left, top, right, bottom) for the border or the rule."""
        if self.bordered:
            return (1, 1, 1, 1)
        return {
            Side.LEFT: (1, 0, 0, 0),
            Side.TOP: (0, 1, 0, 0),
            Side.RIGHT: (0, 0, 1, 0),
            Side.BOTTOM: (0, 0, 0, 1),
        }.get(self.divider, (0, 0, 0, 0))

    def head(self, w: int) -> _Head | None:
        if not self.title:
            return None
        return _Head(self.title, self.subtitle, False, self.actions, w)

    def size(self, t) -> tuple[int, int]:
        l, tp, r, b = self.edge()
        inner_w = max(0, self.width - (l + r + 4))
        h = self.head(inner_w)
        head = h.size(t)[1] + 1 if h is not None else 0
        return (self.width, tp + b + 2 + head + _stack_size(t, self.children, 1)[1])

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        c = t.shown.controls
        l, tp, r, b = self.edge()
        if self.bordered:
            fill(t, buf, paint.inset(area, 1, 1), c.normal.fill)
            frame(t, buf, area, c.normal.border.color)
        elif self.divider is Side.LEFT:
            vline(t, buf, area.x, area.y, area.height, "│", t.track())
        elif self.divider is Side.RIGHT:
            vline(t, buf, area.right - 1, area.y, area.height, "│", t.track())
        elif self.divider is Side.TOP:
            hline(t, buf, area.x, area.y, area.width, "─", t.track())
        elif self.divider is Side.BOTTOM:
            hline(t, buf, area.x, area.bottom - 1, area.width, "─", t.track())
        inner = Rect(area.x + l + 2, area.y + tp + 1, max(0, area.width - (l + r + 4)), max(0, area.height - (tp + b + 2)))
        y = inner.y
        h = self.head(inner.width)
        if h is not None:
            hh = h.size(t)[1]
            h.draw(t, inner.replace(y=y, height=hh), buf)
            y += hh + 1
        _stack_draw(t, self.children, 1, inner.replace(y=y, height=max(0, inner.bottom - y)), buf)


class HeadingLevel(enum.Enum):
    """Which font token a Heading stands for. The terminal has one size: every level is a bold title
    line with a `textDim` subtitle under it."""

    TITLE = "title"
    HEADING = "heading"
    DISPLAY = "display"


class Heading(Draw):
    """The title of a pane, toolbar, section or page: a bold line, an optional `textDim` subtitle
    under it, an optional trailing slot (one column apart) at the end of its width."""

    def __init__(self, text: str, subtitle: str = "", level: HeadingLevel = HeadingLevel.TITLE, centered: bool = False,
                 width: int | None = None, trailing: Iterable[Draw] = ()):
        self.text, self.subtitle, self.level, self.centered, self.width = text, subtitle, level, centered, width
        self.trailing: list[Draw] = list(trailing)

    def head(self) -> _Head:
        return _Head(self.text, self.subtitle, self.centered, self.trailing, self.width)

    def size(self, t) -> tuple[int, int]:
        return self.head().size(t)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        self.head().draw(t, area, buf)


class MenuRow(Draw):
    """A list or menu row: the label, an accent check at the end when `checked`, the hover fill when
    `highlighted`."""

    def __init__(self, label: str, checked: bool = False, highlighted: bool = False, width: int = 30, key: object = None):
        self.label, self.checked, self.highlighted, self.width, self.key = label, checked, highlighted, width, key

    def size(self, t) -> tuple[int, int]:
        return (self.width, 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        if self.highlighted:
            fill(t, buf, area, t.shown.controls.hover.fill)
        text_fit(t, buf, area.x + 1, area.y, self.label, max(0, area.width - 4), t.fg())
        if self.checked:
            put(t, buf, max(0, area.right - 2), area.y, glyphs.CHECK, Ink(t.accent(), bold=True))
        if self.key is not None:
            t.hit(self.key, area)


class Tooltip(Draw):
    """The tooltip surface: a box in the tooltip border color holding the text and, with a shortcut,
    a KeyCombo one column after it. Three rows."""

    def __init__(self, text: str, shortcut: str = ""):
        self.text, self.shortcut = text, shortcut

    def size(self, t) -> tuple[int, int]:
        combo = 0 if not self.shortcut else 1 + KeyCombo(self.shortcut).size(t)[0]
        return (width(self.text) + combo + 4, 3)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        clear(buf, area)
        s = t.shown.surfaces.tooltip
        inner = frame(t, buf, area, s.border.color)
        w = text_fit(t, buf, inner.x + 1, inner.y, self.text, max(0, inner.width - 2), s.text)
        if self.shortcut:
            kc = KeyCombo(self.shortcut)
            kw = kc.size(t)[0]
            kc.draw(t, Rect(inner.x + 2 + w, inner.y, kw, 1).intersection(inner), buf)


def _dialog_button_w(label: str) -> int:
    return max(11, width(label) + 4)


def _dialog_button(t, buf: Buffer, x: int, y: int, label: str, selected: bool, destructive: bool) -> None:
    s = t.shown
    if selected and destructive:
        fill_c, border, i = t.urgent().with_alpha(0.22), t.urgent(), t.urgent()
    elif selected:
        fill_c, border, i = s.surfaces.menu.selected_background, s.surfaces.menu.selected_text, s.surfaces.menu.selected_text
    elif destructive:
        fill_c, border, i = TRANSPARENT, t.urgent().with_alpha(0.56), t.fg()
    else:
        fill_c, border, i = TRANSPARENT, t.fg().with_alpha(0.38), t.fg()
    w = _dialog_button_w(label)
    control(t, buf, x, y, w, Look(fill_c, border, i, False))
    text_center(t, buf, x + 1, y, w - 2, label, Ink(i, bold=selected))


class Dialog(Draw):
    """A modal card on the scrim: title (bold), message, body (those present a blank row apart), then
    a blank row and the buttons at the bottom right (secondary, primary), the selected one in the
    menu's selected look (urgent when destructive).
    Buttons keyed `(key, "cancel")` and `(key, "accept")`."""

    def __init__(self, title: str = "", message: str = "", body: Draw | None = None, labels: tuple[str, str] = ("", ""),
                 destructive: bool = False, selected: int = 1, width: int = 46, scrim: bool = True, key: object = None):
        self.title, self.message, self.body = title, message, body
        self.secondary, self.primary = labels
        self.destructive, self.selected, self.width, self.scrim, self.key = destructive, selected, width, scrim, key

    @staticmethod
    def handle_key(selected: int, key: str) -> int | None:
        """Left/Right/h/l/Tab move the selection; the new one, or None. (Escape and Return are the
        app's: cancel, and the selected button.)"""
        if key in ("left", "h"):
            return 0
        if key in ("right", "l"):
            return 1
        if key in ("tab", "shift+tab"):
            return 1 - min(selected, 1)
        return None

    def card_size(self, t) -> tuple[int, int]:
        inner = max(0, self.width - 6)
        h = 0
        if self.title:
            h += 1
        msg = len(wrap(self.message, inner))
        if self.message:
            h += msg + (1 if h > 0 else 0)
        if self.body is not None:
            h += self.body.size(t)[1] + (1 if h > 0 else 0)
        return (self.width, h + 2 + 4)

    def size(self, t) -> tuple[int, int]:
        return self.card_size(t)

    def labels_shown(self, t) -> tuple[str, str]:
        secondary = self.secondary or t.string("common.cancel", "Cancel")
        primary = self.primary or t.string("common.confirm", "Confirm")
        return secondary, primary

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        s = t.shown
        if self.scrim:
            paint.scrim(t, buf, area, s.roles.scrim)
        cw, ch = self.card_size(t)
        cw, ch = min(cw, area.width), min(ch, area.height)
        card = Rect(area.x + (area.width - cw) // 2, area.y + (area.height - ch) // 2, cw, ch)
        clear(buf, card)
        inner = paint.inset(frame(t, buf, card, s.surfaces.menu.selected_text), 2, 1)
        y = inner.y
        if self.title:
            text_fit(t, buf, inner.x, y, self.title, inner.width, Ink(t.fg(), bold=True))
            y += 1
        if self.message:
            if y > inner.y:
                y += 1
            for line in wrap(self.message, inner.width):
                text_fit(t, buf, inner.x, y, line, inner.width, t.fg())
                y += 1
        if self.body is not None:
            bw, bh = self.body.size(t)
            if y > inner.y:
                y += 1
            self.body.draw(t, Rect(inner.x, y, min(bw, inner.width), bh), buf)
        secondary, primary = self.labels_shown(t)
        by = max(0, inner.bottom - 1)
        sw, pw = _dialog_button_w(secondary), _dialog_button_w(primary)
        px = max(0, inner.right - pw)
        sx = max(0, px - (sw + 1))
        _dialog_button(t, buf, sx, by, secondary, self.selected == 0, False)
        _dialog_button(t, buf, px, by, primary, self.selected == 1, self.destructive)
        if self.key is not None:
            t.hit((self.key, "cancel"), Rect(sx, by, sw, 1))
            t.hit((self.key, "accept"), Rect(px, by, pw, 1))


def ConfirmDialog(message: str, **kw) -> Dialog:
    """A Dialog with no title, the message and a destructive Confirm, selected."""
    kw.setdefault("destructive", True)
    kw.setdefault("selected", 1)
    return Dialog("", message, **kw)
