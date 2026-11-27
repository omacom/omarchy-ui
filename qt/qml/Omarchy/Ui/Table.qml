import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A data table: a header row of dim bold captions (the sorted column carries a
// sort chevron), then body rows of plain text, cut with "…" when too wide for
// their column. Rows are drawn by the table, not delegates, so every table in
// every app reads the same.
//
//   columns: [{ title: "Name" }, { title: "Size", width: 70, align: "right" }]
//            A column without a width shares the space the fixed ones leave.
//   rows:    [["assets", "Folder", "—"], ...]  (strings, one per column)
//
// The table does not sort: it shows `sortColumn`/`sortAscending` and asks for
// a new order with `sortRequested(column)`. `selectedRow` is the chosen row
// (selected fill, bold); `cursorRow` is the keyboard cursor (hover fill),
// which the pointer moves too, so there is one highlight on screen at a time.
// Up/Down/Home/End move the cursor, Return/Space choose it.
FocusScope {
  id: root

  property var columns: []
  property var rows: []
  property int sortColumn: -1
  property bool sortAscending: true
  property int selectedRow: -1
  property int cursorRow: -1
  // Force the hover look on `cursorRow` without focus, for still renders and
  // panels that drive the cursor themselves.
  property bool hasCursor: false
  property bool separators: true

  property color foreground: Commons.Color.foreground
  property color accent: Commons.Color.accent

  signal rowActivated(int row)
  signal sortRequested(int column)

  readonly property int rowHeight: Style.spacing.popupRowHeight
  readonly property int cellPadding: Style.spacing.controlPaddingX
  readonly property bool _showCursor: hasCursor || activeFocus || body.hovering

  // Column widths: fixed ones as given, the rest share what is left.
  readonly property var columnWidths: {
    var fixed = 0, flex = 0
    for (var i = 0; i < columns.length; i++) {
      var w = Number(columns[i].width || 0)
      if (w > 0) fixed += w
      else flex++
    }
    var share = flex > 0 ? Math.max(0, Math.floor((width - fixed) / flex)) : 0
    var out = []
    for (var j = 0; j < columns.length; j++) {
      var cw = Number(columns[j].width || 0)
      out.push(cw > 0 ? cw : share)
    }
    return out
  }

  implicitWidth: 400
  implicitHeight: header.height + rows.length * rowHeight
  activeFocusOnTab: true

  function moveCursor(delta) {
    if (rows.length === 0) return
    var from = cursorRow < 0 ? (delta > 0 ? -1 : rows.length) : cursorRow
    cursorRow = Math.max(0, Math.min(rows.length - 1, from + delta))
    body.positionViewAtIndex(cursorRow, ListView.Contain)
  }

  function activate(row) {
    if (row < 0 || row >= rows.length) return
    selectedRow = row
    rowActivated(row)
  }

  Keys.onUpPressed: moveCursor(-1)
  Keys.onDownPressed: moveCursor(1)
  Keys.onPressed: function(event) {
    if (event.key === Qt.Key_Home) { cursorRow = rows.length > 0 ? 0 : -1; event.accepted = true }
    else if (event.key === Qt.Key_End) { cursorRow = rows.length - 1; event.accepted = true }
    else if (event.key === Qt.Key_PageDown) { moveCursor(Math.max(1, Math.floor(body.height / rowHeight))); event.accepted = true }
    else if (event.key === Qt.Key_PageUp) { moveCursor(-Math.max(1, Math.floor(body.height / rowHeight))); event.accepted = true }
  }
  Keys.onReturnPressed: activate(cursorRow)
  Keys.onEnterPressed: activate(cursorRow)
  Keys.onSpacePressed: activate(cursorRow)

  // ---- Header
  Item {
    id: header
    width: root.width
    height: root.rowHeight

    Row {
      Repeater {
        model: root.columns

        Item {
          id: headerCell
          required property var modelData
          required property int index
          readonly property bool sorted: root.sortColumn === index
          readonly property bool alignRight: modelData.align === "right"

          width: root.columnWidths[index] || 0
          height: header.height

          Row {
            id: headerContent
            spacing: Style.spacing.xs
            layoutDirection: headerCell.alignRight ? Qt.RightToLeft : Qt.LeftToRight
            x: headerCell.alignRight ? headerCell.width - root.cellPadding - width : root.cellPadding
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, headerCell.width - root.cellPadding * 2)

            Text {
              anchors.verticalCenter: parent.verticalCenter
              width: Math.min(implicitWidth, headerCell.width - root.cellPadding * 2 - (sortGlyph.visible ? sortGlyph.width + headerContent.spacing : 0))
              textFormat: Text.PlainText
              text: headerCell.modelData.title || ""
              elide: Text.ElideRight
              color: Commons.Color.textDim
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
              font.bold: true
            }

            Text {
              id: sortGlyph
              visible: headerCell.sorted
              anchors.verticalCenter: parent.verticalCenter
              textFormat: Text.PlainText
              text: root.sortAscending ? "󰅃" : "󰅀"
              color: Commons.Color.textDim
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
            }
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.sortRequested(headerCell.index)
          }
        }
      }
    }

    Rectangle {
      anchors.bottom: parent.bottom
      width: parent.width
      height: Style.spacing.hairline
      color: Commons.Color.track
    }
  }

  // ---- Body
  ListView {
    id: body

    property bool hovering: false

    y: header.height
    width: root.width
    height: Math.max(0, root.height - header.height)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: root.rows.length

    delegate: Item {
      id: rowItem
      required property int index
      readonly property var cells: root.rows[index] || []
      readonly property bool isCursor: root._showCursor && root.cursorRow === index
      readonly property bool isSelected: root.selectedRow === index

      width: body.width
      height: root.rowHeight

      Rectangle {
        anchors.fill: parent
        radius: Style.cornerRadius
        color: rowItem.isCursor
          ? Style.hoverFillFor(root.foreground, root.accent)
          : (rowItem.isSelected ? Style.selectedFillFor(root.foreground, root.accent) : "transparent")
      }

      Row {
        Repeater {
          model: root.columns

          Item {
            id: cell
            required property var modelData
            required property int index
            width: root.columnWidths[index] || 0
            height: root.rowHeight

            Text {
              x: root.cellPadding
              width: Math.max(0, cell.width - root.cellPadding * 2)
              anchors.verticalCenter: parent.verticalCenter
              horizontalAlignment: cell.modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
              textFormat: Text.PlainText
              text: String(rowItem.cells[cell.index] === undefined ? "" : rowItem.cells[cell.index])
              elide: Text.ElideRight
              color: rowItem.isSelected ? Style.selectedStateColor(root.foreground, root.accent) : root.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              font.bold: rowItem.isSelected
            }
          }
        }
      }

      Rectangle {
        readonly property bool nextHighlighted: (root.selectedRow === rowItem.index + 1)
          || (root._showCursor && root.cursorRow === rowItem.index + 1)
        visible: root.separators && rowItem.index < root.rows.length - 1
          && !rowItem.isSelected && !rowItem.isCursor && !nextHighlighted
        anchors.bottom: parent.bottom
        width: parent.width
        height: Style.spacing.hairline
        color: Commons.Color.track
      }

      MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: { body.hovering = true; root.cursorRow = rowItem.index }
        onExited: body.hovering = false
        onClicked: {
          root.forceActiveFocus()
          root.activate(rowItem.index)
        }
      }
    }
  }
}
