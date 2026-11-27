import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// An app's navigation sidebar: sections, each a SectionHeader over rows of
// glyph + label. One row is selected (selected fill, bold); `cursorId` is the
// keyboard cursor (hover fill), which the pointer moves too. `collapsed` folds
// it to a rail of glyphs.
//
//   sections: [{ title: "Library", items: [{ id: "all", label: "All files", glyph: "󰉋" }, ...] }, ...]
//
// Up/Down walk every row across sections, Return/Space choose; the app owns
// `selectedId` and hears `activated(id)`.
FocusScope {
  id: root

  property var sections: []
  property string selectedId: ""
  property string cursorId: ""
  property bool collapsed: false
  // Force the hover look on `cursorId` without focus, for still renders.
  property bool hasCursor: false

  property color foreground: Commons.Color.foreground
  property color accent: Commons.Color.accent

  signal activated(string id)

  readonly property int rowHeight: Style.spacing.popupRowHeight
  readonly property int padding: Style.spacing.sm
  readonly property int rowInset: Style.spacing.controlPaddingX
  // Glyph slot + gap: labels line up whatever each glyph's width.
  readonly property int labelOffset: Style.font.icon + Style.spacing.controlGap
  readonly property int railWidth: padding * 2 + rowInset * 2 + Style.font.icon
  readonly property bool _showCursor: hasCursor || activeFocus || _hovering
  property bool _hovering: false

  readonly property var _ids: {
    var out = []
    for (var i = 0; i < sections.length; i++) {
      var items = sections[i].items || []
      for (var j = 0; j < items.length; j++) out.push(String(items[j].id))
    }
    return out
  }

  function moveCursor(delta) {
    if (_ids.length === 0) return
    var at = _ids.indexOf(cursorId)
    var from = at < 0 ? (delta > 0 ? -1 : _ids.length) : at
    cursorId = _ids[Math.max(0, Math.min(_ids.length - 1, from + delta))]
  }

  implicitWidth: collapsed ? railWidth : Style.space(200)
  implicitHeight: column.implicitHeight + padding * 2
  activeFocusOnTab: true

  Keys.onUpPressed: moveCursor(-1)
  Keys.onDownPressed: moveCursor(1)
  Keys.onReturnPressed: if (cursorId !== "") activated(cursorId)
  Keys.onEnterPressed: if (cursorId !== "") activated(cursorId)
  Keys.onSpacePressed: if (cursorId !== "") activated(cursorId)

  Column {
    id: column
    x: root.padding
    y: root.padding
    width: root.width - root.padding * 2
    spacing: Style.spacing.md

    Repeater {
      model: root.sections

      Column {
        id: section
        required property var modelData
        width: column.width

        PanelSectionHeader {
          visible: !root.collapsed && text !== ""
          text: section.modelData.title || ""
          leftPadding: root.rowInset
          bottomPadding: Style.spacing.xxs
          width: parent.width
          elide: Text.ElideRight
        }

        Repeater {
          model: section.modelData.items || []

          Item {
            id: rowItem
            required property var modelData
            readonly property string rowId: String(modelData.id)
            readonly property bool isCursor: root._showCursor && root.cursorId === rowId
            readonly property bool isSelected: root.selectedId === rowId
            readonly property color ink: isSelected ? Style.selectedStateColor(root.foreground, root.accent) : root.foreground

            width: section.width
            height: root.rowHeight

            Rectangle {
              anchors.fill: parent
              radius: Style.cornerRadius
              color: rowItem.isCursor
                ? Style.hoverFillFor(root.foreground, root.accent)
                : (rowItem.isSelected ? Style.selectedFillFor(root.foreground, root.accent) : "transparent")
            }

            OpticalGlyph {
              x: root.rowInset
              width: Style.font.icon
              height: parent.height
              text: rowItem.modelData.glyph || ""
              color: rowItem.isSelected ? rowItem.ink : Commons.Color.textDim
              fontSize: Style.font.icon
            }

            Text {
              visible: !root.collapsed
              x: root.rowInset + root.labelOffset
              width: Math.max(0, parent.width - x - root.rowInset)
              anchors.verticalCenter: parent.verticalCenter
              textFormat: Text.PlainText
              text: rowItem.modelData.label || ""
              elide: Text.ElideRight
              color: rowItem.ink
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              font.bold: rowItem.isSelected
            }

            MouseArea {
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onEntered: { root._hovering = true; root.cursorId = rowItem.rowId }
              onExited: root._hovering = false
              onClicked: {
                root.forceActiveFocus()
                root.activated(rowItem.rowId)
              }
            }
          }
        }
      }
    }
  }
}
