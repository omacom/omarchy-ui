import QtQuick
import QtQuick.Layouts
import Omarchy.Theme
import Omarchy.Theme as Commons
import Omarchy.Ui
import ".."

Section {
  name: "pickers"
  title: "Pickers"

  RowLayout {
    spacing: Style.spacing.xxl

    MultiSelect {
      Layout.alignment: Qt.AlignTop
      label: "Bar modules"
      options: ["Clock", "Battery", "Network", "Volume", "Bluetooth"]
      values: ["Clock", "Battery", "Network", "Volume"]
    }

    // Held open: the popup draws in the window overlay, below the trigger, inside this box.
    Item {
      Layout.alignment: Qt.AlignTop
      implicitWidth: open.implicitWidth
      implicitHeight: 280
      MultiSelect {
        id: open
        width: parent.width
        label: "Notify for"
        options: ["Calendar", "Mail", "Messages", "Updates", "Battery"]
        values: ["Mail", "Updates"]
        // Once the window has its height, which the popup clamps to.
        Timer { interval: 100; running: true; onTriggered: open.open() }
      }
    }
  }

  RowLayout {
    spacing: Style.spacing.controlGap
    ColorWell { swatch: Commons.Color.accent }
    ColorWell { swatch: Commons.Color.success; hasCursor: true }
    ColorWell { swatch: Commons.Color.urgent }
  }

  SwatchStrip {
    colors: [Commons.Color.accent, Commons.Color.urgent, Commons.Color.warning, Commons.Color.success,
             Commons.Color.foreground, Commons.Color.textDim, Commons.Color.textFaint, Commons.Color.muted]
    currentIndex: 2
  }

  // Held open over the content it would cover, so the scrim shows.
  Item {
    implicitWidth: 420
    implicitHeight: 240

    Card {
      anchors.fill: parent
      title: "Displays"
      Column {
        Layout.fillWidth: true
        PopupMenuRow { text: "Built-in display"; checked: true }
        PopupMenuRow { text: "External monitor" }
        PopupMenuRow { text: "Projector" }
      }
    }

    Dialog {
      anchors.fill: parent
      opened: true
      title: "Discard changes?"
      message: "Your edits to Tokyo Night will be lost."
      secondaryText: "Keep editing"
      primaryText: "Discard"
      destructive: true
    }
  }
}
