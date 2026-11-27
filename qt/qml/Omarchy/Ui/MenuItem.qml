import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// One row of a Menu: an optional leading glyph, the label, a dim shortcut hint at the right, and a
// trailing mark: an accent check (or a radio dot when `exclusive`) when `checked`, or a chevron when
// the row opens a submenu. Under the pointer or the menu's keyboard cursor it takes the menu
// surface's selected background and text. Disabled rows are faint and inert; destructive rows
// are urgent.
//
//   MenuItem { text: "Save"; iconText: ""; shortcut: "Ctrl+S"; onTriggered: doc.save() }
//   MenuItem { text: "Export"; submenu: Menu { MenuItem { text: "PDF" } } }
//
// Inside a Menu, the leading-glyph and mark columns are reserved for every row when any row
// uses them, so labels and hints line up.
Rectangle {
  id: root

  property string text: ""
  property string iconText: ""
  property string shortcut: ""
  property bool checkable: false
  property bool checked: false
  // A radio mark: one of a set of exclusive choices.
  property bool exclusive: false
  property bool destructive: false
  property Item submenu: null
  property bool hasSubmenu: submenu !== null
  // Forces the cursor look, for still renders and panels with their own cursor model.
  property bool hasCursor: false

  readonly property bool isMenuItem: true
  readonly property Item menu: parent && parent.menu !== undefined ? parent.menu : null
  readonly property bool highlighted: menu !== null && menu.currentItem === root
  readonly property bool reserveIcon: menu ? menu.hasIcons : iconText !== ""
  readonly property bool reserveMark: menu ? menu.hasMarks : (checkable || hasSubmenu)
  readonly property bool hot: enabled && (mouse.containsMouse || hasCursor || highlighted)
  readonly property color ink: !enabled ? Commons.Color.textFaint
    : destructive ? Commons.Color.urgent
    : hot ? Commons.Color.menu.selectedText
    : Commons.Color.menu.text

  signal triggered()

  // Runs the row: opens its submenu, or emits triggered() and lets the menu close.
  function activate() {
    if (!enabled) return
    if (hasSubmenu) {
      if (menu) menu.openSubmenu(root)
      return
    }
    root.triggered()
    if (menu) menu.itemTriggered(root)
  }

  readonly property real _padX: Style.spacing.controlPaddingX
  readonly property real _gap: Style.spacing.controlGap
  readonly property real _iconBox: Style.font.icon
  readonly property real _markBox: Style.font.icon

  implicitHeight: Style.spacing.popupRowHeight
  implicitWidth: _padX * 2
    + (reserveIcon ? _iconBox + _gap : 0)
    + Math.ceil(label.implicitWidth)
    + (shortcut !== "" ? Style.spacing.huge + Math.ceil(hint.implicitWidth) : 0)
    + (reserveMark ? _gap + _markBox : 0)
  width: parent ? parent.width : implicitWidth
  height: visible ? implicitHeight : 0
  radius: Math.min(4, Style.cornerRadius)
  color: mouse.pressed && enabled ? Style.pressedFill
    : hot ? Commons.Color.menu.selectedBackground
    : "transparent"
  Behavior on color { ColorAnimation { duration: Style.duration(120) } }

  OpticalGlyph {
    visible: root.iconText !== ""
    x: root._padX
    width: root._iconBox
    height: root.height
    text: root.iconText
    fontSize: Style.font.icon
    color: root.ink
  }

  Text {
    id: label
    x: root._padX + (root.reserveIcon ? root._iconBox + root._gap : 0)
    anchors.verticalCenter: parent.verticalCenter
    textFormat: Text.PlainText
    text: root.text
    color: root.ink
    font.family: Style.font.family
    font.pixelSize: Style.font.body
  }

  Text {
    id: hint
    visible: root.shortcut !== ""
    anchors.right: parent.right
    anchors.rightMargin: root._padX + (root.reserveMark ? root._markBox + root._gap : 0)
    anchors.baseline: label.baseline
    textFormat: Text.PlainText
    text: root.shortcut
    color: root.enabled ? Commons.Color.textDim : Commons.Color.textFaint
    font.family: Style.font.family
    font.pixelSize: Style.font.bodySmall
  }

  OpticalGlyph {
    readonly property bool mark: root.checkable && root.checked
    visible: (mark && !root.exclusive) || root.hasSubmenu
    x: root.width - root._padX - root._markBox
    width: root._markBox
    height: root.height
    text: mark ? "✓" : ""
    fontSize: mark ? Style.font.body : Style.font.iconSmall
    color: !root.enabled ? Commons.Color.textFaint : mark ? Commons.Color.accent : Commons.Color.textDim
  }

  Rectangle {
    readonly property int dot: Style.space(6)
    visible: root.checkable && root.checked && root.exclusive
    x: root.width - root._padX - root._markBox + Math.round((root._markBox - dot) / 2)
    anchors.verticalCenter: parent.verticalCenter
    width: dot
    height: dot
    radius: dot / 2
    color: !root.enabled ? Commons.Color.textFaint : Commons.Color.accent
  }

  MouseArea {
    id: mouse
    anchors.fill: parent
    hoverEnabled: true
    enabled: root.enabled
    cursorShape: Qt.PointingHandCursor
    onEntered: if (root.menu) root.menu.currentIndex = root.menu.rows.indexOf(root)
    onClicked: root.activate()
  }
}
