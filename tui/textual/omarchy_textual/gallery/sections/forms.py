"""Forms, Pickers."""

from __future__ import annotations

from textual.app import ComposeResult

from ...widgets import (
    Checkbox,
    Col,
    ColorWell,
    Dropdown,
    MultiSelect,
    NumberField,
    PasswordField,
    RadioGroup,
    Row,
    SettingRow,
    SwatchStrip,
    TextArea,
    ToggleSwitch,
)
from ..page import Section
from .controls import THEMES

MODULES = [("clock", "Clock"), ("battery", "Battery"), ("network", "Network"), ("volume", "Volume"), ("bluetooth", "Bluetooth")]
NOTIFY = [("calendar", "Calendar"), ("mail", "Mail"), ("messages", "Messages"), ("updates", "Updates"), ("battery", "Battery")]


class Forms(Section):
    NAME, TITLE = "forms", "Forms"

    def compose(self) -> ComposeResult:
        with Col(gap=1):
            with Col(gap=1):
                yield SettingRow("Night light", "Warm the screen after sunset", ToggleSwitch(True, id="night"), width=60)
                yield SettingRow("Theme", "Applies to every app", Dropdown(THEMES, 0, width=23, focusable=False), width=60)
                yield SettingRow("Gaps", "Space between windows", NumberField(10, range=(0, 40), width=15, focusable=False), width=60)
            with Row(gap=4):
                with Col(gap=0):
                    yield Checkbox("Show seconds", False, id="check-0")
                    yield Checkbox("Show date", True, id="check-1")
                    # The cursor's look, pinned: a specimen, not a stop for Tab.
                    yield Checkbox("Show weekday", False, cursor=True, focusable=False, id="check-2")
                    yield Checkbox("Show week number", False, disabled=True, id="check-3")
                    yield Checkbox("Twelve-hour clock", True, description="Show AM and PM", id="check-4")
                yield RadioGroup([("top", "Top"), ("bottom", "Bottom"), ("hidden", "Hidden")], "bottom", id="radio")
            with Row(gap=1):
                yield PasswordField("", placeholder="Password", width=28, focusable=False)
                yield PasswordField("hunter22", revealable=True, width=28, id="password")
                yield PasswordField("letmein", error="Wrong password, try again", width=28, focusable=False)
            yield TextArea("Back up your dotfiles\nbefore switching themes,\nthen run omarchy update.", width=60, focusable=False)


class Pickers(Section):
    NAME, TITLE = "pickers", "Pickers"

    def compose(self) -> ComposeResult:
        from ...widgets import Box, Card, Dialog, MenuRow, Stack

        with Col(gap=1):
            with Row(gap=2):
                yield MultiSelect(MODULES, ["clock", "battery", "network", "volume"], label="Bar modules", width=33,
                                  focusable=False, id="modules")
                with Box(width=33, height=11):
                    yield MultiSelect(NOTIFY, ["mail", "updates"], label="Notify for", width=33, open=True, id="notify")
            with Row(gap=1):
                yield ColorWell("accent", focusable=False)
                yield ColorWell("success", cursor=True, focusable=False)
                yield ColorWell("urgent", focusable=False)
            yield SwatchStrip(["accent", "urgent", "warning", "success", "fg", "dim", "faint", "muted"], 2, id="swatch")
            with Stack(width=53, height=13):
                with Card(title="Displays", width=53):
                    with Col(gap=0):
                        yield MenuRow("Built-in display", checked=True, width=47, focusable=False)
                        yield MenuRow("External monitor", width=47, focusable=False)
                        yield MenuRow("Projector", width=47, focusable=False)
                yield Dialog("Discard changes?", message="Your edits to Tokyo Night will be lost.",
                             labels=("Keep editing", "Discard"), destructive=True, selected=1, width=46, id="discard")
