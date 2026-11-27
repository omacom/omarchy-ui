import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// The grip a row is dragged by to reorder it: a 2×3 grid of dots, textDim at rest and the
// foreground under the pointer, the keyboard cursor or focus. Dots are `space(3)` square,
// `space(3)` apart, rounded by `min(4, radius)`.
//
// Dragging reports pointer travel since the press (`moved`); the list owning the rows works out
// the drop slot and shows a DropIndicator there. Up/Down while focused
// asks to move the row by one (`moveRequested`).
Item {
  id: root

  property bool hasCursor: false
  readonly property bool dragging: area.pressed
  readonly property bool hot: hasCursor || activeFocus || area.containsMouse || dragging

  signal pressed()
  signal moved(real dx, real dy)
  signal released()
  signal moveRequested(int delta)

  readonly property real dot: Style.space(3)
  readonly property real dotGap: Style.space(3)

  implicitWidth: Style.space(16)
  implicitHeight: Style.space(22)
  activeFocusOnTab: true

  Grid {
    id: grip
    columns: 2
    spacing: root.dotGap
    anchors.centerIn: parent
    Repeater {
      model: 6
      Rectangle {
        width: root.dot
        height: root.dot
        radius: Math.min(Math.min(4, Style.cornerRadius), root.dot / 2)
        color: root.hot ? Commons.Color.foreground : Commons.Color.textDim
      }
    }
  }

  MouseArea {
    id: area
    anchors.fill: parent
    hoverEnabled: true
    preventStealing: true
    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
    property point origin
    onPressed: function(mouse) {
      root.forceActiveFocus()
      origin = mapToItem(null, mouse.x, mouse.y)
      root.pressed()
    }
    onPositionChanged: function(mouse) {
      if (!pressed) return
      var p = mapToItem(null, mouse.x, mouse.y)
      root.moved(p.x - origin.x, p.y - origin.y)
    }
    onReleased: root.released()
  }

  Keys.onPressed: function(event) {
    if (event.key === Qt.Key_Up) root.moveRequested(-1)
    else if (event.key === Qt.Key_Down) root.moveRequested(1)
    else return
    event.accepted = true
  }
}
