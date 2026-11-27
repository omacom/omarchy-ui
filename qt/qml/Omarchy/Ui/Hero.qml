import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// The head of a panel: a big glyph, a bold title, a dim status line (urgent on error), and an
// optional control on the trailing edge, centered against the labels. What the bluetooth,
// network, audio, power and monitor panels draw by hand above their lists (PanelHero is the
// shell's slot-based version of the same layout).
//
//   Hero {
//     glyph: "󰂯"; title: "Bluetooth"; status: "2 devices connected"
//     trailing: Component { ToggleSwitch { checked: true } }
//   }
Item {
  id: root

  property string glyph: ""
  property string title: ""
  property string status: ""
  property bool error: false
  property real glyphOpacity: 1.0
  property color foreground: Commons.Color.foreground
  property Component trailing: null

  readonly property real trailingInset: trailingLoader.item && trailingLoader.item.visible ? trailingLoader.width + Style.space(12) : 0

  readonly property real glyphSlotWidth: root.glyph !== "" ? Style.font.display : 0
  readonly property real glyphGap: root.glyph !== "" ? Style.space(14) : 0

  width: parent ? parent.width : implicitWidth
  implicitWidth: glyphSlotWidth + glyphGap + Math.max(titleText.implicitWidth, statusText.implicitWidth) + trailingInset
  implicitHeight: Math.max(heroGlyph.height, labels.implicitHeight, trailingLoader.implicitHeight)

  OpticalGlyph {
    id: heroGlyph
    visible: root.glyph !== ""
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
    width: root.glyphSlotWidth
    height: Math.max(Style.font.displayLarge, Math.ceil(Style.font.display * 1.35))
    text: root.glyph
    color: root.foreground
    opacity: root.glyphOpacity
    fontSize: Style.font.display
  }

  Column {
    id: labels
    anchors.left: heroGlyph.right
    anchors.leftMargin: root.glyphGap
    anchors.right: parent.right
    anchors.rightMargin: root.trailingInset
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.space(2)

    Text {
      id: titleText
      textFormat: Text.PlainText
      width: parent.width
      text: root.title
      color: root.foreground
      font.family: Style.font.family
      font.pixelSize: Style.font.title
      font.bold: true
      elide: Text.ElideRight
    }

    Text {
      id: statusText
      textFormat: Text.PlainText
      visible: root.status !== ""
      width: parent.width
      text: root.status.toUpperCase()
      color: root.error ? Commons.Color.urgent : Commons.Color.textDim
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
      font.bold: true
      font.letterSpacing: 1.2
      elide: Text.ElideRight
    }
  }

  Loader {
    id: trailingLoader
    sourceComponent: root.trailing
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
  }
}
