"""The pieces media and image editors need (spec/terminal.md, Editing): a port of
`omarchy-ratatui`'s `ui/editing.rs`, cell for cell. Handles and the playhead are accent; clips,
lanes and swatches foreground glass."""

from __future__ import annotations

import enum
import math
from dataclasses import dataclass, replace
from typing import Iterable

from ..color import Rgba, over, rround
from .base import Draw
from .buffer import Buffer, Rect
from .controls import TextField
from .paint import Ink, edges, fill, hline, put, text, text_fit, width

__all__ = [
    "Clip",
    "ColorPicker",
    "CropFrame",
    "DragHandle",
    "DropIndicator",
    "Edge",
    "Hsva",
    "Scrubber",
    "Timeline",
    "TrimHandle",
    "clock_label",
    "ruler_step",
]


def clock_label(secs: float, tenths: bool = False) -> str:
    """m:ss (h:mm:ss from an hour; tenths below a 1 s step)."""
    if tenths:
        t = rround(secs * 10.0)
        return f"{t // 600}:{(t // 10) % 60:02}.{t % 10}"
    s = rround(secs)
    if s >= 3600:
        return f"{s // 3600}:{(s // 60) % 60:02}:{s % 60:02}"
    return f"{s // 60}:{s % 60:02}"


# Timeline --------------------------------------------------------------------------------------


@dataclass
class Clip:
    start: float
    end: float
    label: str


_STEPS = (0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0)


def ruler_step(duration: float, cols: int) -> float:
    """The first step that keeps major ticks at least 11 columns apart."""
    per_sec = cols / max(duration, 1e-9)
    for s in _STEPS:
        if s * per_sec >= 11.0:
            return s
    return 3600.0


def _trim_cap(t, buf: Buffer, x: int, y: int, h: int, hot: bool) -> None:
    """Accent cells `h` rows tall, the grip `│` in the background on the middle 40% (60% hot)."""
    ink = t.hot_accent() if hot else t.accent()
    fill(t, buf, Rect(x, y, 1, h), ink)
    g = min(max(rround(h * (0.6 if hot else 0.4)), 1), h)
    top = y + (h - g) // 2
    for dy in range(g):
        put(t, buf, x, top + dy, "│", t.bg())


class Timeline(Draw):
    """A ruler over clip lanes with a playhead. Clips keyed `(key, ("clip", track, i))`, the ruler
    `(key, "ruler")`."""

    def __init__(self, duration: float, position: float = 0.0, tracks: Iterable[Iterable[Clip]] = (),
                 selected: tuple[int, int] | None = None, cursor: bool = False, size: tuple[int, int] = (90, 10),
                 key: object = None):
        self.duration = max(duration, 0.001)
        self.position = position
        self.tracks = [list(c) for c in tracks]
        self.selected, self.cursor, self.key = selected, cursor, key
        self.width, self.height = size

    @staticmethod
    def x_of(tm: float, duration: float, w: int) -> int:
        return min(max(rround(tm / duration * w), 0), max(0, w - 1))

    @staticmethod
    def time_at(col: int, duration: float, w: int) -> float:
        return min(max(col / max(w, 1) * duration, 0.0), duration)

    @staticmethod
    def handle_key(position: float, duration: float, cols: int, key: str) -> float | None:
        """Left/Right ±1 s (Shift: a labeled step), Home/End; the new position, or None."""
        shift = key.startswith("shift+")
        k = key[6:] if shift else key
        step = ruler_step(duration, cols) if shift else 1.0
        if k in ("left", "h"):
            p = position - step
        elif k in ("right", "l"):
            p = position + step
        elif k == "home":
            p = 0.0
        elif k == "end":
            p = duration
        else:
            return None
        return min(max(p, 0.0), duration)

    def size(self, t) -> tuple[int, int]:
        return (self.width, self.height)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        c = t.shown.controls
        w = area.width
        step = ruler_step(self.duration, w)
        tenths = step < 1.0
        minor = step / 5.0
        n_minor = math.floor(self.duration / minor)
        for i in range(n_minor + 1):
            tm = i * minor
            x = area.x + Timeline.x_of(tm, self.duration, w)
            put(t, buf, x, area.y + 1, "│" if i % 5 == 0 else "╷", t.faint())
        n_major = math.floor(self.duration / step)
        for i in range(n_major + 1):
            tm = i * step
            label = clock_label(tm, tenths)
            lw = width(label)
            cx = Timeline.x_of(tm, self.duration, w) - int((lw - 1) / 2)
            lx = min(max(cx, 0), w - lw)
            text(t, buf, area.x + lx, area.y, label, lw, t.dim())
        lanes_h = max(0, area.height - 2)
        n = max(len(self.tracks), 1)
        lane_h = lanes_h // n
        for ti, clips in enumerate(self.tracks):
            ly = area.y + 2 + ti * lane_h
            lh = lanes_h - lane_h * (n - 1) if ti == n - 1 else lane_h
            if lh == 0 or w < 2:
                continue
            fill(t, buf, Rect(area.x, ly, w, lh), c.normal.fill)
            for ci, clip in enumerate(clips):
                x0 = Timeline.x_of(clip.start, self.duration, w)
                x1 = max(Timeline.x_of(clip.end, self.duration, w), x0 + 2)
                cw = min(x1 - x0, w - x0)
                selected = self.selected == (ti, ci)
                r = Rect(area.x + x0, ly, cw, lh)
                if selected:
                    fill(t, buf, r, c.selected.fill)
                    _trim_cap(t, buf, r.x, ly, lh, False)
                    _trim_cap(t, buf, r.right - 1, ly, lh, False)
                else:
                    fill(t, buf, r, c.normal.fill)
                    for dy in range(lh):
                        edges(t, buf, r.x, ly + dy, cw, c.normal.border.color)
                text_fit(t, buf, r.x + 2, ly + (lh - 1) // 2, clip.label, max(0, cw - 4), Ink(t.fg(), bold=selected))
                if self.key is not None:
                    t.hit((self.key, ("clip", ti, ci)), r)
        px = area.x + Timeline.x_of(self.position, self.duration, w)
        head = t.hot_accent() if self.cursor else t.accent()
        put(t, buf, px, area.y + 1, "▼", head)
        for dy in range(2, area.height):
            put(t, buf, px, area.y + dy, "│", head)
        if self.key is not None:
            t.hit((self.key, "ruler"), Rect(area.x, area.y, w, 2))


# Scrubber --------------------------------------------------------------------------------------


class Scrubber(Draw):
    """`0:42 ━━━━━━●────── 2:10`: the times in `textDim`, the Slider's track (no edges) a column
    from each. The knob is never bold: `cursor` is taken for the GUI bindings' API and draws nothing
    (the hot knob's scale has no cell form). The bar keyed `(key, "bar")`."""

    def __init__(self, position: float, duration: float, width: int = 45, cursor: bool = False, key: object = None):
        self.position, self.duration = position, max(duration, 0.001)
        self.width, self.cursor, self.key = width, cursor, key

    @staticmethod
    def handle_key(position: float, duration: float, key: str) -> float | None:
        """Left/Right ±5 s (Shift ±30 s), Home/End; the new position, or None."""
        shift = key.startswith("shift+")
        k = key[6:] if shift else key
        step = 30.0 if shift else 5.0
        if k in ("left", "h"):
            p = position - step
        elif k in ("right", "l"):
            p = position + step
        elif k == "home":
            p = 0.0
        elif k == "end":
            p = duration
        else:
            return None
        return min(max(p, 0.0), duration)

    def size(self, t) -> tuple[int, int]:
        return (self.width, 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        a = clock_label(self.position, False)
        b = clock_label(self.duration, False)
        aw, bw = width(a), width(b)
        text(t, buf, area.x, area.y, a, aw, t.dim())
        text(t, buf, max(0, area.right - bw), area.y, b, bw, t.dim())
        bar = Rect(area.x + aw + 1, area.y, max(0, area.width - (aw + bw + 2)), 1)
        w = bar.width
        if w < 2:
            return
        k = rround(min(max(self.position / self.duration, 0.0), 1.0) * (w - 1))
        for i in range(w):
            if i < k:
                put(t, buf, bar.x + i, bar.y, "━", t.fg())
            elif i > k:
                put(t, buf, bar.x + i, bar.y, "─", t.track())
        put(t, buf, bar.x + k, bar.y, "●", t.fg())
        if self.key is not None:
            t.hit((self.key, "bar"), bar)


# TrimHandle, CropFrame, DragHandle, DropIndicator ------------------------------------------------


class Edge(enum.Enum):
    LEFT = "left"
    RIGHT = "right"
    TOP = "top"
    BOTTOM = "bottom"
    TOP_LEFT = "top-left"
    TOP_RIGHT = "top-right"
    BOTTOM_LEFT = "bottom-left"
    BOTTOM_RIGHT = "bottom-right"


def _crop_cells(edge: Edge) -> list[tuple[int, int, str]]:
    return {
        Edge.TOP_LEFT: [(0, 0, "┏"), (1, 0, "━"), (0, 1, "┃")],
        Edge.TOP_RIGHT: [(0, 0, "┓"), (-1, 0, "━"), (0, 1, "┃")],
        Edge.BOTTOM_LEFT: [(0, 0, "┗"), (1, 0, "━"), (0, -1, "┃")],
        Edge.BOTTOM_RIGHT: [(0, 0, "┛"), (-1, 0, "━"), (0, -1, "┃")],
        Edge.TOP: [(0, 0, "━"), (1, 0, "━")],
        Edge.BOTTOM: [(0, 0, "━"), (1, 0, "━")],
        Edge.LEFT: [(0, 0, "┃")],
        Edge.RIGHT: [(0, 0, "┃")],
    }[edge]


class TrimHandle(Draw):
    """A draggable in/out handle. Trim (`crop=False`): an accent cap a column wide, `height` rows,
    the grip cut in the background. Crop: the heavy accent glyphs a CropFrame draws."""

    def __init__(self, edge: Edge, crop: bool = False, cursor: bool = False, height: int | None = None):
        self.edge, self.crop, self.cursor = edge, crop, cursor
        self.height = height if height is not None else (1 if crop else 3)

    def size(self, t) -> tuple[int, int]:
        if self.crop:
            if self.edge in (Edge.TOP, Edge.BOTTOM):
                return (2, 1)
            if self.edge in (Edge.LEFT, Edge.RIGHT):
                return (1, 1)
            return (2, 2)
        return (1, self.height)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        if not self.crop:
            _trim_cap(t, buf, area.x, area.y, area.height, self.cursor)
            return
        i = t.hot_accent() if self.cursor else t.accent()
        if self.edge == Edge.TOP_RIGHT:
            ax, ay = area.right - 1, area.y
        elif self.edge == Edge.BOTTOM_LEFT:
            ax, ay = area.x, area.bottom - 1
        elif self.edge == Edge.BOTTOM_RIGHT:
            ax, ay = area.right - 1, area.bottom - 1
        else:
            ax, ay = area.x, area.y
        for dx, dy, s in _crop_cells(self.edge):
            put(t, buf, ax + dx, ay + dy, s, Ink(i, bold=True))


class CropFrame(Draw):
    """A crop rectangle: a light accent outline, rule-of-thirds guides in `track`, the eight crop
    handles in heavy accent strokes."""

    def __init__(self, width: int, height: int, cursor: Edge | None = None):
        self.width, self.height, self.cursor = width, height, cursor

    def size(self, t) -> tuple[int, int]:
        return (self.width, self.height)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        w, h = area.width, area.height
        if w < 4 or h < 3:
            return
        for i in (1, 2):
            gx = area.x + (w * i) // 3
            for dy in range(1, h - 1):
                put(t, buf, gx, area.y + dy, "┆", t.track())
        for i in (1, 2):
            gy = area.y + (h * i) // 3
            for dx in range(1, w - 1):
                x = area.x + dx
                cross = any(area.x + (w * j) // 3 == x for j in (1, 2))
                put(t, buf, x, gy, "┼" if cross else "┄", t.track())
        hline(t, buf, area.x, area.y, w, "─", t.accent())
        hline(t, buf, area.x, area.bottom - 1, w, "─", t.accent())
        for dy in range(h):
            put(t, buf, area.x, area.y + dy, "│", t.accent())
            put(t, buf, area.right - 1, area.y + dy, "│", t.accent())
        mx, my = area.x + (w - 2) // 2, area.y + (h - 1) // 2

        def place(e: Edge, r: Rect) -> None:
            TrimHandle(e, crop=True, cursor=self.cursor == e).draw(t, r, buf)

        place(Edge.TOP_LEFT, area)
        place(Edge.TOP_RIGHT, area)
        place(Edge.BOTTOM_LEFT, area)
        place(Edge.BOTTOM_RIGHT, area)
        place(Edge.TOP, Rect(mx, area.y, 2, 1))
        place(Edge.BOTTOM, Rect(mx, area.bottom - 1, 2, 1))
        place(Edge.LEFT, Rect(area.x, my, 1, 1))
        place(Edge.RIGHT, Rect(area.right - 1, my, 1, 1))


class DragHandle(Draw):
    """The grip a row is dragged by: `⠿`, `textDim`, the foreground when hot."""

    def __init__(self, cursor: bool = False):
        self.cursor = cursor

    def size(self, t) -> tuple[int, int]:
        return (1, 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        put(t, buf, area.x, area.y, "⠿", t.fg() if self.cursor else t.dim())


class DropIndicator(Draw):
    """Where a dragged row will land: `━` in accent across a row of its own."""

    def __init__(self, width: int):
        self.width = width

    def size(self, t) -> tuple[int, int]:
        return (self.width, 1)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        hline(t, buf, area.x, area.y, area.width, "━", t.accent())


# ColorPicker -----------------------------------------------------------------------------------


@dataclass(frozen=True)
class Hsva:
    """Hue (0–1 turns), saturation, value, alpha, all 0–1."""

    h: float
    s: float
    v: float
    a: float = 1.0

    def rgb8(self) -> tuple[int, int, int]:
        """The 0–255 channels, by the same arithmetic as every binding's picker."""
        h6 = (self.h - math.floor(self.h)) * 6.0
        i = math.floor(h6)
        f = h6 - i
        s, v = self.s, self.v
        p, q, tt = v * (1.0 - s), v * (1.0 - s * f), v * (1.0 - s * (1.0 - f))
        c = {0: (v, tt, p), 1: (q, v, p), 2: (p, v, tt), 3: (p, q, v), 4: (tt, p, v)}.get(int(i) % 6, (v, p, q))
        return tuple(min(255, max(0, rround(x * 255.0))) for x in c)  # type: ignore[return-value]

    def rgba(self) -> Rgba:
        r, g, b = self.rgb8()
        return Rgba(r, g, b, min(255, max(0, rround(self.a * 255.0))))

    def hex(self) -> str:
        """`#rrggbb`, or `#rrggbbaa` below full alpha."""
        r, g, b = self.rgb8()
        out = f"#{r:02x}{g:02x}{b:02x}"
        if self.a < 1.0:
            out += f"{min(255, max(0, rround(self.a * 255.0))):02x}"
        return out

    def with_(self, **kw) -> "Hsva":
        return replace(self, **kw)

    @staticmethod
    def from_hex(hex: str, keep: float = 0.0) -> "Hsva | None":
        """From `#rrggbb[aa]`; the hue kept from `keep` when the color is gray."""
        try:
            c = Rgba.parse(hex)
        except ValueError:
            return None
        r, g, b = c.r / 255.0, c.g / 255.0, c.b / 255.0
        mx, mn = max(r, g, b), min(r, g, b)
        d = mx - mn
        if d == 0.0:
            h = keep
        elif mx == r:
            h = ((g - b) / d) % 6.0 / 6.0
        elif mx == g:
            h = ((b - r) / d + 2.0) / 6.0
        else:
            h = ((r - g) / d + 4.0) / 6.0
        return Hsva(h, 0.0 if mx == 0.0 else d / mx, mx, c.a / 255.0)


PALETTE = ("red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground")


class ColorPicker(Draw):
    """An HSV picker in half-block pixels: the square (saturation across, value down) with its
    knob, a hue strip and an alpha strip, the preview and the hex field, the theme palette's
    swatches. Swatches keyed `(key, ("swatch", i))`, the square `(key, "square")`."""

    def __init__(self, color: Hsva, alpha: bool = True, size: tuple[int, int] = (38, 16),
                 palette: Iterable[str] = PALETTE, cursor: bool = False, hex: str | None = None, key: object = None):
        self.color, self.alpha = color, alpha
        self.width, self.height = size
        self.palette = list(palette)
        self.cursor, self.hex, self.key = cursor, hex, key

    def swatches(self, t) -> list[Rgba]:
        p = t.shown.palette
        out = []
        for n in self.palette:
            if n == "foreground":
                out.append(p.foreground)
            elif n == "background":
                out.append(p.background)
            elif n == "accent":
                out.append(p.accent)
            elif n in p.named:
                out.append(p.named[n])
        return out

    @staticmethod
    def handle_key(c: Hsva, key: str) -> Hsva | None:
        """Arrows move the square's knob 1% (Shift 10%); the new color, or None."""
        shift = key.startswith("shift+")
        k = key[6:] if shift else key
        d = 0.1 if shift else 0.01
        if k in ("left", "h"):
            return c.with_(s=min(max(c.s - d, 0.0), 1.0))
        if k in ("right", "l"):
            return c.with_(s=min(max(c.s + d, 0.0), 1.0))
        if k in ("up", "k"):
            return c.with_(v=min(max(c.v + d, 0.0), 1.0))
        if k in ("down", "j"):
            return c.with_(v=min(max(c.v - d, 0.0), 1.0))
        return None

    def size(self, t) -> tuple[int, int]:
        return (self.width, self.height)

    def draw(self, t, area: Rect, buf: Buffer) -> None:
        strips = 6 if self.alpha else 3
        sq_w = max(0, area.width - strips)
        sq_h = max(0, area.height - 5)
        if sq_w < 2 or sq_h < 1:
            return
        ph = sq_h * 2
        color = self.color

        def pixel(i: int, j: int) -> Rgba:
            s = i / max(sq_w - 1, 1)
            v = 1.0 - j / max(ph - 1, 1)
            return Hsva(color.h, s, v, 1.0).rgba()

        for row in range(sq_h):
            for i in range(sq_w):
                x, y = area.x + i, area.y + row
                fill(t, buf, Rect(x, y, 1, 1), pixel(i, row * 2 + 1))
                put(t, buf, x, y, "▀", pixel(i, row * 2))
        picked = replace(color, a=1.0).rgba()
        kx = area.x + rround(min(max(color.s, 0.0), 1.0) * (sq_w - 1))
        ky = area.y + rround((1.0 - min(max(color.v, 0.0), 1.0)) * (sq_h - 1))
        fill(t, buf, Rect(kx, ky, 1, 1), t.bg())
        put(t, buf, kx, ky, "●", picked)
        hx = area.x + sq_w + 1
        for row in range(sq_h):
            for k, half in ((0, True), (1, False)):
                j = row * 2 + k
                c = Hsva(j / max(ph - 1, 1), 1.0, 1.0, 1.0).rgba()
                for dx in range(2):
                    if half:
                        put(t, buf, hx + dx, area.y + row, "▀", c)
                    else:
                        fill(t, buf, Rect(hx + dx, area.y + row, 1, 1), c)
        hy = area.y + rround((color.h % 1.0) * (sq_h - 1))
        fill(t, buf, Rect(hx, hy, 2, 1), t.bg())
        put(t, buf, hx, hy, "◀", t.fg())
        put(t, buf, hx + 1, hy, "▶", t.fg())
        if self.alpha:
            ax = hx + 3
            under = over(t.track(), t.bg())
            for row in range(sq_h):
                for k, half in ((0, True), (1, False)):
                    j = row * 2 + k
                    a = 1.0 - j / max(ph - 1, 1)
                    c = over(picked.with_alpha(a), under)
                    for dx in range(2):
                        if half:
                            put(t, buf, ax + dx, area.y + row, "▀", c)
                        else:
                            fill(t, buf, Rect(ax + dx, area.y + row, 1, 1), c)
            ay = area.y + rround((1.0 - min(max(color.a, 0.0), 1.0)) * (sq_h - 1))
            fill(t, buf, Rect(ax, ay, 2, 1), t.bg())
            put(t, buf, ax, ay, "◀", t.fg())
            put(t, buf, ax + 1, ay, "▶", t.fg())
        fy = area.y + sq_h + 1
        shown = over(color.rgba(), t.bg())
        put(t, buf, area.x, fy, "█", shown)
        put(t, buf, area.x + 1, fy, "█", shown)
        hx_text = self.hex if self.hex is not None else color.hex()
        fw = max(0, area.width - 3)
        TextField(hx_text, width=fw, cursor=self.cursor).draw(t, Rect(area.x + 3, fy, fw, 1), buf)
        py = fy + 2
        for i, c in enumerate(self.swatches(t)):
            x = area.x + i * 3
            if x + 2 > area.right:
                break
            put(t, buf, x, py, "█", c)
            put(t, buf, x + 1, py, "█", c)
            if (c.r, c.g, c.b) == (picked.r, picked.g, picked.b):
                put(t, buf, x, py + 1, "▔", t.fg())
                put(t, buf, x + 1, py + 1, "▔", t.fg())
            if self.key is not None:
                t.hit((self.key, ("swatch", i)), Rect(x, py, 2, 1))
        if self.key is not None:
            t.hit((self.key, "square"), Rect(area.x, area.y, sq_w, sq_h))
