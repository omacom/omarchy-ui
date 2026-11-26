import QtQuick
import QtTest
import Omarchy.Theme as Theme
import Omarchy.Test

// The Qt binding's live-switch test (spec/following.md): a simulated `omarchy theme set` reaches
// the running QML, Tokens.changed fires once with the new tokens, and the colors fade on the
// spec's curve (or, under OMARCHY_UI_REDUCE_MOTION=1, land at once). Run in a desktop of its own
// (see ../tests/main.cpp): tools/live/run build/qt-tests/omarchy-qt-tests -input qt/tests-live
TestCase {
  id: test
  name: "Follow"

  readonly property string tokyo: "#1a1b26"
  readonly property string latte: "#eff1f5"

  // Every background and card background the window would have painted, and what the
  // changed hook saw when it ran.
  property var backgrounds: []
  property var cards: []
  property string hookSaw: ""
  property bool hookSawFading: true

  SignalSpy { id: changes; target: Theme.Tokens; signalName: "changed" }

  Connections {
    target: Theme.Color
    function onBackgroundChanged() { test.backgrounds.push(String(Theme.Color.background)) }
  }
  Connections {
    target: Theme.Color.popups
    function onBackgroundChanged() { test.cards.push(String(Theme.Color.popups.background)) }
  }
  Connections {
    target: Theme.Tokens
    function onChanged(tokens) {
      // After the new tokens are in effect: Color already reads them.
      test.hookSaw = String(Theme.Color.background)
      test.hookSawFading = Theme.Color.fading
    }
  }

  function between(list, a, b) {
    return list.some(function (c) { return c !== a && c !== b })
  }

  function test_a_theme_switch_reaches_qml_once() {
    if (!LiveSwitch.available) skip("needs tools/live/run")
    compare(String(Theme.Color.background), tokyo)
    compare(Theme.Tokens.revision, 0)
    compare(Theme.Tokens.current.palette.background, tokyo + "ff")
    wait(300)
    compare(changes.count, 0, "the initial load is not a change")

    backgrounds = []
    cards = []
    verify(LiveSwitch.to("catppuccin-latte"))
    changes.wait(5000)
    compare(changes.count, 1)
    var tokens = changes.signalArguments[0][0]
    compare(tokens.palette.background, latte + "ff", "the hook gets the new tokens")
    compare(tokens.mode, "light")
    compare(Theme.Tokens.revision, 1)
    compare(Theme.Tokens.current.palette.background, latte + "ff")
    compare(hookSaw, latte, "Color already reads the new theme in the hook")
    verify(!hookSawFading, "and not a frame of the fade")

    if (Theme.Style.reduceMotion) {
      verify(!Theme.Color.fading, "no fade under reduced motion")
      compare(String(Theme.Color.background), latte)
      verify(!between(backgrounds, tokyo, latte), "nothing in between: " + backgrounds)
    } else {
      verify(Theme.Color.fading, "the colors fade")
      tryVerify(function () { return !Theme.Color.fading }, 2000)
      verify(between(backgrounds, tokyo, latte), "the background passed through the colors between: " + backgrounds)
      verify(between(cards, tokyo, latte), "and the cards (a literal in shell.toml) fade with it: " + cards)
    }
    compare(String(Theme.Color.background), latte, "it lands exactly")
    compare(String(Theme.Color.popups.background), latte)

    wait(600)
    compare(changes.count, 1, "one switch is one change")

    // Removing palette keys during a live reload should use the same defaults as a fresh load.
    changes.clear()
    verify(LiveSwitch.writeColors('mode = "light"\n'))
    changes.wait(5000)
    compare(changes.count, 1)
    compare(Theme.Tokens.revision, 2)
    var defaults = {
      accent: "#cacccc", background: "#101315", foreground: "#cacccc",
      urgent: "#a55555", muted: "#cacccc", success: "#7f9f6f", warning: "#c0a060",
    }
    var palette = changes.signalArguments[0][0].palette
    for (var key in defaults)
      compare(palette[key], defaults[key] + "ff", key + " token")
    tryCompare(Theme.Color, "fading", false, 2000)
    for (var key in defaults)
      compare(String(Theme.Color[key]), defaults[key], key + " after the fade")
  }
}
