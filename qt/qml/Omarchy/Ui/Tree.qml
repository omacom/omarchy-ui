import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A tree of rows, FileTree-style: each level indented by `huge`, a chevron on
// nodes with children (right when collapsed, down when expanded), an optional
// glyph, then the label.
//
//   nodes:    [{ id: "qt", label: "qt", glyph: "󰉋", children: [...] }, ...]
//   expanded: ["qt", ...]  the ids whose children show
//
// The app owns `expanded`, `selectedId` and `cursorId`: the tree asks with
// `toggled(id)` (chevron click, Left/Right) and `activated(id)` (click,
// Return/Space). `cursorId` is the keyboard cursor (hover fill), which the
// pointer moves too; `selectedId` is the chosen row (selected fill, bold).
FocusScope {
  id: root

  property var nodes: []
  property var expanded: []
  property string selectedId: ""
  property string cursorId: ""
  // Force the hover look on `cursorId` without focus, for still renders.
  property bool hasCursor: false

  property color foreground: Commons.Color.foreground
  property color accent: Commons.Color.accent

  signal toggled(string id)
  signal activated(string id)

  readonly property int rowHeight: Style.spacing.popupRowHeight
  readonly property bool _showCursor: hasCursor || activeFocus || list.hovering

  // The visible rows, depth first: { id, label, glyph, depth, branch, open }.
  readonly property var visibleRows: {
    var out = []
    var open = {}
    for (var i = 0; i < expanded.length; i++) open[expanded[i]] = true
    function walk(list, depth) {
      for (var j = 0; j < list.length; j++) {
        var n = list[j]
        var kids = n.children || []
        var isOpen = kids.length > 0 && open[n.id] === true
        out.push({ id: String(n.id), label: String(n.label || ""), glyph: String(n.glyph || ""), depth: depth, branch: kids.length > 0, open: isOpen })
        if (isOpen) walk(kids, depth + 1)
      }
    }
    walk(nodes, 0)
    return out
  }

  function cursorIndex() {
    for (var i = 0; i < visibleRows.length; i++)
      if (visibleRows[i].id === cursorId) return i
    return -1
  }

  function moveCursor(delta) {
    if (visibleRows.length === 0) return
    var at = cursorIndex()
    var from = at < 0 ? (delta > 0 ? -1 : visibleRows.length) : at
    var next = Math.max(0, Math.min(visibleRows.length - 1, from + delta))
    cursorId = visibleRows[next].id
    list.positionViewAtIndex(next, ListView.Contain)
  }

  implicitWidth: 240
  implicitHeight: visibleRows.length * rowHeight
  activeFocusOnTab: true

  Keys.onUpPressed: moveCursor(-1)
  Keys.onDownPressed: moveCursor(1)
  Keys.onRightPressed: { var r = visibleRows[cursorIndex()]; if (r && r.branch && !r.open) toggled(r.id) }
  Keys.onLeftPressed: { var r = visibleRows[cursorIndex()]; if (r && r.branch && r.open) toggled(r.id) }
  Keys.onReturnPressed: if (cursorId !== "") activated(cursorId)
  Keys.onEnterPressed: if (cursorId !== "") activated(cursorId)
  Keys.onSpacePressed: if (cursorId !== "") activated(cursorId)

  ListView {
    id: list

    property bool hovering: false

    anchors.fill: parent
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: root.visibleRows

    delegate: Item {
      id: rowItem
      required property var modelData
      required property int index
      readonly property bool isCursor: root._showCursor && root.cursorId === modelData.id
      readonly property bool isSelected: root.selectedId === modelData.id

      width: list.width
      height: root.rowHeight

      Rectangle {
        anchors.fill: parent
        radius: Style.cornerRadius
        color: rowItem.isCursor
          ? Style.hoverFillFor(root.foreground, root.accent)
          : (rowItem.isSelected ? Style.selectedFillFor(root.foreground, root.accent) : "transparent")
      }

      // Chevron slot, then glyph, then label.
      Item {
        id: chevronSlot
        x: Style.spacing.md + rowItem.modelData.depth * Style.spacing.huge
        width: Style.spacing.xxxl
        height: parent.height

        OpticalGlyph {
          visible: rowItem.modelData.branch
          anchors.fill: parent
          text: rowItem.modelData.open ? "󰅀" : "󰅂"
          color: Commons.Color.textDim
          fontSize: Style.font.iconSmall
        }

        MouseArea {
          anchors.fill: parent
          enabled: rowItem.modelData.branch
          cursorShape: Qt.PointingHandCursor
          onClicked: root.toggled(rowItem.modelData.id)
        }
      }

      OpticalGlyph {
        id: glyph
        visible: rowItem.modelData.glyph !== ""
        x: chevronSlot.x + chevronSlot.width + Style.spacing.sm
        width: Style.font.icon
        height: parent.height
        text: rowItem.modelData.glyph
        color: rowItem.isSelected ? Style.selectedStateColor(root.foreground, root.accent) : Commons.Color.textDim
        fontSize: Style.font.icon
      }

      Text {
        readonly property real labelX: chevronSlot.x + chevronSlot.width + Style.spacing.sm
          + (glyph.visible ? Style.font.icon + Style.spacing.md : 0)
        x: labelX
        width: Math.max(0, parent.width - labelX - Style.spacing.md)
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: rowItem.modelData.label
        elide: Text.ElideRight
        color: rowItem.isSelected ? Style.selectedStateColor(root.foreground, root.accent) : root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: rowItem.isSelected
      }

      MouseArea {
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: { list.hovering = true; root.cursorId = rowItem.modelData.id }
        onExited: list.hovering = false
        onClicked: {
          root.forceActiveFocus()
          root.activated(rowItem.modelData.id)
        }
        onDoubleClicked: if (rowItem.modelData.branch) root.toggled(rowItem.modelData.id)
      }
    }
  }
}
