import QtQuick
import QtTest
import Omarchy.Theme as Theme
import Omarchy.Test

TestCase {
  name: "FirstTheme"

  function test_first_theme_is_followed() {
    compare(String(Theme.Color.background), "#101315")
    compare(Theme.Tokens.revision, 0)

    // No theme or override directories existed when the source armed its watcher.
    verify(LiveSwitch.to("tokyo-night"))
    tryCompare(Theme.Color, "background", "#1a1b26", 5000)
    compare(Theme.Tokens.revision, 1)

    // Once the theme tree exists, its normal directory swaps still reach the app.
    verify(LiveSwitch.to("catppuccin-latte"))
    tryCompare(Theme.Color, "background", "#eff1f5", 5000)
    compare(Theme.Tokens.revision, 2)
  }
}
