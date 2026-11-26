import QtQuick
import QtTest
import Omarchy.Theme as Theme

TestCase {
  name: "HyprlandReconnect"

  function test_refreshes_after_connect_without_configuration_event() {
    compare(Theme.Style.cornerRadius, 0)
    compare(Theme.Tokens.revision, 0)

    // The fixture creates both sockets after the runner has observed this line.
    console.log("start Hyprland fixture")
    tryCompare(Theme.Style, "cornerRadius", 8, 7000)
    compare(Theme.Tokens.revision, 1)
    compare(Theme.Tokens.current.radius, 8)
  }
}
