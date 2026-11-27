"""Every gallery section in every fixture theme, snapped the way `--snap` does (headless, from
Textual's composed screen): each at its spec'd size (spec/terminal.md, Gallery: 96 columns, the
rows below), in the canonical form (`omarchy-ansi check` and `canon` agree byte for byte), with
the page background left to the terminal; and a given size crops or pads.

    build/textual/venv/bin/python tui/textual/tests/snapshots.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from support import ROOT, fixture, fixtures  # noqa: E402

#: The natural height of every section (spec/terminal.md, Gallery), 96 columns wide.
SIZES = {
    "buttons": 7,
    "toggles": 11,
    "inputs": 8,
    "slider": 7,
    "surfaces": 34,
    "lists": 25,
    "tiles": 25,
    "progress": 12,
    "messages": 26,
    "menus": 26,
    "navigation": 15,
    "forms": 26,
    "pickers": 34,
    "data": 12,
    "layout": 27,
    "timeline": 26,
    "color": 20,
    "composed": 37,
    "tokens": 18,
}

ANSI = str(ROOT / "target/release/omarchy-ansi")


def snap(theme: str, section: str, cols: int = 96, rows: int = 0) -> str:
    from omarchy_textual.gallery.app import GalleryApp
    from omarchy_textual.snap import snap_app

    app = GalleryApp(section=section, snap=True, theme=fixture(theme))
    return snap_app(app, cols, rows, natural=lambda a: a.query_one("#section").get_content_height(None, None, cols))


def check(theme: str) -> list[str]:
    os.environ["OMARCHY_UI_REDUCE_MOTION"] = "1"
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        for section, rows in SIZES.items():
            text = snap(theme, section)
            path = Path(tmp) / f"{section}.ans"
            path.write_text(text, encoding="utf-8")
            ok = subprocess.run([ANSI, "check", str(path), "--size", f"96x{rows}"], capture_output=True, text=True)
            if ok.returncode != 0:
                errors.append(f"{theme} {section}: {ok.stdout.strip()} {ok.stderr.strip()}")
                continue
            canon = subprocess.run([ANSI, "canon", str(path)], capture_output=True, text=True).stdout
            if canon != text:
                errors.append(f"{theme} {section}: not canonical")
            # The page background is the terminal's: never painted.
            if "\x1b[0m " not in text and not any(line.startswith(" ") for line in text.splitlines()):
                errors.append(f"{theme} {section}: no default-background cell")
        # An explicit size crops or pads.
        small = snap(theme, "lists", 40, 5)
        p = Path(tmp) / "small.ans"
        p.write_text(small, encoding="utf-8")
        if subprocess.run([ANSI, "check", str(p), "--size", "40x5"], capture_output=True).returncode != 0:
            errors.append(f"{theme}: --size 40x5 isn't 40x5")
        big = snap(theme, "buttons", 96, 12)
        p.write_text(big, encoding="utf-8")
        if subprocess.run([ANSI, "check", str(p), "--size", "96x12"], capture_output=True).returncode != 0:
            errors.append(f"{theme}: --size 96x12 isn't 96x12")
    return errors


def main() -> int:
    themes = fixtures()
    with ProcessPoolExecutor(max_workers=min(8, os.cpu_count() or 2)) as pool:
        results = list(pool.map(check, themes))
    errors = [e for r in results for e in r]
    for e in errors:
        print(f"snapshots: FAIL: {e}", file=sys.stderr)
    if errors:
        return 1
    print(f"snapshots: {len(SIZES)} sections × {len(themes)} themes at their sizes, canonical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
