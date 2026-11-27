import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A draggable in/out handle. Two kinds:
//
//  - trim (default): the cap on a clip's `left` or `right` edge (omareel's OmTrimHandle). An
//    accent block `space(8)` wide, square on the inside, `min(4, radius)` on the outside, with a
//    grip line cut into it that grows under the pointer or cursor.
//  - crop: one of the eight handles on a crop rectangle. `edge` is `left`, `right`, `top`,
//    `bottom` (a bar) or `top-left`, `top-right`, `bottom-left`, `bottom-right` (an L bracket).
//
// Accent, because a handle belongs to the chosen clip or region. The parent places it, inside the
// clip or rectangle at that edge or corner; the handle reports pointer travel since the press
// (`moved`) and arrow-key nudges, and the parent turns those into times or rectangles.
Item {
  id: root

  property string edge: "left"
  property bool crop: false
  property bool hasCursor: false
  property color color: Commons.Color.accent
  // The grip line, cut into a trim cap.
  property color gripColor: Commons.Color.background

  readonly property bool dragging: area.pressed
  readonly property bool hot: area.containsMouse || hasCursor || activeFocus || dragging
  readonly property bool corner: edge.indexOf("-") > 0
  readonly property bool horizontalBar: edge === "top" || edge === "bottom"
  readonly property real thickness: crop ? Style.space(4) : Style.space(8)
  readonly property real length: Style.space(18)
  readonly property real smallRadius: Math.min(4, Style.cornerRadius)
  // Under the pointer the handle mixes a quarter of the foreground into its color.
  readonly property color drawColor: hot ? Qt.tint(root.color, Qt.alpha(Commons.Color.foreground, 0.25)) : root.color

  // Pointer travel since the press, in this item's pixels.
  signal moved(real dx, real dy)
  signal released()
  // Arrow keys while focused: 1px, 10px with Shift.
  signal nudged(real dx, real dy)

  implicitWidth: crop ? (corner || horizontalBar ? length : thickness) : thickness
  implicitHeight: crop ? (corner || !horizontalBar ? length : thickness) : Style.space(40)
  activeFocusOnTab: true

  // ---- trim cap

  Item {
    anchors.fill: parent
    visible: !root.crop

    Rectangle {
      anchors.fill: parent
      radius: root.smallRadius
      color: root.drawColor
    }
    // Square off the inner side.
    Rectangle {
      x: root.edge === "right" ? 0 : parent.width / 2
      width: parent.width / 2
      height: parent.height
      color: root.drawColor
    }
    Rectangle {
      width: Math.max(1, Style.space(2))
      height: Math.round(parent.height * (root.hot ? 0.6 : 0.4))
      x: Math.round((parent.width - width) / 2)
      y: Math.round((parent.height - height) / 2)
      radius: Math.min(root.smallRadius, width / 2)
      color: root.gripColor
    }
  }

  // ---- crop handle: a bar, or two bars meeting in a corner.
  // Outer edges flush against the 1px crop border stay square at r8 so they
  // don't pull away into pinched undercut notches.

  readonly property real _cropRadius: Math.min(root.smallRadius, root.thickness / 2)

  Rectangle {
    visible: root.crop
    x: 0
    y: root.corner && root.edge.indexOf("bottom") === 0 ? root.height - height : 0
    width: root.corner || root.horizontalBar ? root.width : root.thickness
    height: root.corner || root.horizontalBar ? root.thickness : root.height
    topLeftRadius: (root.edge === "bottom" || root.edge === "right" || root.edge === "bottom-right") ? root._cropRadius : 0
    topRightRadius: (root.edge === "bottom" || root.edge === "left" || root.edge === "bottom-left") ? root._cropRadius : 0
    bottomLeftRadius: (root.edge === "top" || root.edge === "right" || root.edge === "top-right") ? root._cropRadius : 0
    bottomRightRadius: (root.edge === "top" || root.edge === "left" || root.edge === "top-left") ? root._cropRadius : 0
    color: root.drawColor
  }
  Rectangle {
    visible: root.crop && root.corner
    x: root.edge.indexOf("right") > 0 ? root.width - width : 0
    y: 0
    width: root.thickness
    height: root.height
    topLeftRadius: root.edge === "bottom-right" ? root._cropRadius : 0
    topRightRadius: root.edge === "bottom-left" ? root._cropRadius : 0
    bottomLeftRadius: root.edge === "top-right" ? root._cropRadius : 0
    bottomRightRadius: root.edge === "top-left" ? root._cropRadius : 0
    color: root.drawColor
  }

  MouseArea {
    id: area
    // A little more than the visible handle to grab.
    anchors.fill: parent
    anchors.margins: -Style.spacing.sm
    hoverEnabled: true
    preventStealing: true
    cursorShape: {
      if (root.edge === "left" || root.edge === "right") return Qt.SizeHorCursor
      if (root.edge === "top" || root.edge === "bottom") return Qt.SizeVerCursor
      if (root.edge === "top-left" || root.edge === "bottom-right") return Qt.SizeFDiagCursor
      return Qt.SizeBDiagCursor
    }
    property point origin
    onPressed: function(mouse) {
      root.forceActiveFocus()
      origin = mapToItem(null, mouse.x, mouse.y)
    }
    onPositionChanged: function(mouse) {
      if (!pressed) return
      var p = mapToItem(null, mouse.x, mouse.y)
      root.moved(p.x - origin.x, p.y - origin.y)
    }
    onReleased: root.released()
  }

  Keys.onPressed: function(event) {
    var step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
    var dx = 0, dy = 0
    if (event.key === Qt.Key_Left) dx = -step
    else if (event.key === Qt.Key_Right) dx = step
    else if (event.key === Qt.Key_Up) dy = -step
    else if (event.key === Qt.Key_Down) dy = step
    else return
    if (!root.crop) dy = 0
    if (root.crop && (root.edge === "top" || root.edge === "bottom")) dx = 0
    if (root.crop && (root.edge === "left" || root.edge === "right")) dy = 0
    if (dx === 0 && dy === 0) return
    root.nudged(dx, dy)
    event.accepted = true
  }
}
