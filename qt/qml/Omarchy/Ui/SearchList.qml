import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// The type-ahead overlay the shell builds seven times (clipboard, emojis, the menu, the app
// grid, reminders): a filter line, the matching rows under a keyboard cursor, an optional detail
// pane beside them, and an empty state when nothing matches. Drawn on the [menu] surface.
//
//   SearchList {
//     width: 560; height: 400
//     showDetail: true
//     items: all.filter(i => SearchList.matches(i.title, query))   // the caller filters
//     onQueryChanged: refilter()
//     onActivated: function(index) { open(items[index]) }
//     onCloseRequested: overlay.close()
//   }
//
// Items are { title, subtitle, glyph, value, detail }. Filtering is the caller's: pass only what
// matches `query`. matches(text, query) is the plain case-insensitive substring test, for callers
// with nothing better. Typing edits the query (CursorModel.printable decides what is text, plus
// Space, so j and k are text here); Up/Down, Page Up/Down, Home and End move the cursor; Return
// activates; Delete asks to delete; Escape clears the query, or asks to close when it is empty.
FocusScope {
  id: root

  property string query: ""
  property string placeholderText: Commons.I18n.t("common.search", "Search...")
  property var items: []
  // The detail pane beside the list: the cursor item's title and detail, or `detail` instead.
  property bool showDetail: false
  property Component detail: null
  property real detailWidth: Math.floor(body.width * 0.45)
  // Shown when there are no items: "No matches for “query”", or `emptyText` when the query is empty.
  property string emptyGlyph: String.fromCodePoint(0xF0349)
  property string emptyText: ""
  property string noMatchesText: Commons.I18n.t("common.noMatchesFor", "No matches for “%1”")

  readonly property alias cursor: cursorModel
  readonly property alias filterInput: input
  readonly property int cursorIndex: cursorModel.cursorActive && items.length > 0 ? cursorModel.selectedIndex : -1
  readonly property var currentItem: cursorIndex >= 0 && cursorIndex < items.length ? items[cursorIndex] : null

  signal activated(int index)
  signal deleteRequested(int index)
  signal closeRequested()

  // Case-insensitive substring: the clipboard's filter.
  function matches(text, query) {
    return String(text || "").toLowerCase().indexOf(String(query || "").toLowerCase()) >= 0
  }

  readonly property int _headerHeight: Math.max(Style.space(34), Style.font.heading + Style.spacing.controlPaddingY * 2)
  readonly property real _gap: Style.spacing.xxl
  readonly property bool _detailShown: showDetail && items.length > 0
  readonly property real _listWidth: _detailShown ? body.width - detailWidth - _gap * 2 - 1 : body.width

  implicitWidth: Style.space(480)
  implicitHeight: Style.space(360)

  onItemsChanged: cursorModel.settle()
  // A new query starts the cursor at the first match again, as the shell's overlays do.
  onQueryChanged: cursorModel.selectedIndex = 0

  CursorModel {
    id: cursorModel
    sections: [{ name: "results", count: root.items.length }]
    cursorActive: true
    referenceItem: root
    onMoved: function(section, index, byKey) { if (byKey) scroll.ensureVisible(rows.itemAt(index)) }
    onActivated: function(section, index) { if (root.items.length > 0) root.activated(index) }
    onDeleteRequested: function(section, index) { if (root.items.length > 0) root.deleteRequested(index) }
  }

  BorderSurface {
    id: card
    anchors.fill: parent
    color: Commons.Color.menu.background
    borderSpec: Border.surfaceSpec("menu", "border", Commons.Color.menu.border, Math.max(1, Style.space(2)))
    radius: Style.cornerRadius
    padding: Style.spacing.panelPadding

    // Sees every key before the filter line; what it leaves unaccepted is typed there.
    Item {
      id: keys
      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
          if (root.query !== "") root.query = ""
          else root.closeRequested()
          event.accepted = true
          return
        }
        if (event.text === " " || cursorModel.printable(event) !== "") return
        if (event.key === Qt.Key_Backspace) return
        cursorModel.handleKey(event)
      }
    }

    Item {
      id: header
      x: card.contentLeftInset
      y: card.contentTopInset
      width: card.width - card.contentLeftInset - card.contentRightInset
      height: root._headerHeight

      FilterInput {
        id: input
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        keyHandler: keys
        text: root.query
        color: Commons.Color.menu.text
        font.family: Style.font.family
        font.pixelSize: Style.font.heading
        onTextChanged: if (text !== root.query) root.query = text
      }

      Text {
        textFormat: Text.PlainText
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text === "" && input.preeditText === ""
        text: root.placeholderText
        color: Commons.Color.textFaint
        font.family: Style.font.family
        font.pixelSize: Style.font.heading
        elide: Text.ElideRight
      }
    }

    Item {
      id: body
      x: card.contentLeftInset
      y: header.y + header.height + Style.spacing.md
      width: header.width
      height: card.height - card.contentBottomInset - y

      ScrollArea {
        id: scroll
        visible: root.items.length > 0
        width: root._listWidth
        height: body.height
        maxHeight: body.height
        spacing: Style.spacing.xxs

        Repeater {
          id: rows
          model: root.items
          delegate: ListRow {
            id: row
            required property var modelData
            required property int index
            glyph: modelData.glyph || ""
            title: modelData.title || ""
            subtitle: modelData.subtitle || ""
            value: modelData.value || ""
            hasCursor: cursorModel.isAt("results", index)
            onPointerMoved: function(mouse) { cursorModel.pointerMoved(row, mouse, "results", index) }
            onClicked: { cursorModel.set("results", index); root.activated(index) }
          }
        }
      }

      // The detail pane: a 1px track rule, then the cursor item.
      Rectangle {
        visible: root._detailShown
        x: root._listWidth + root._gap
        width: 1
        height: body.height
        color: Commons.Color.track
      }

      Item {
        id: detailPane
        visible: root._detailShown
        x: root._listWidth + root._gap * 2 + 1
        width: body.width - x
        height: body.height
        clip: true

        Column {
          visible: root.detail === null
          y: Style.spacing.rowPaddingX / 2
          width: parent.width
          spacing: Style.spacing.sm

          Text {
            textFormat: Text.PlainText
            width: parent.width
            text: root.currentItem ? (root.currentItem.title || "") : ""
            color: Commons.Color.menu.text
            font.family: Style.font.family
            font.pixelSize: Style.font.title
            font.bold: true
            elide: Text.ElideRight
          }
          Text {
            textFormat: Text.PlainText
            width: parent.width
            text: root.currentItem ? (root.currentItem.detail || "") : ""
            color: Commons.Color.textDim
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            wrapMode: Text.Wrap
          }
        }

        Loader {
          width: parent.width
          sourceComponent: root.detail
        }
      }

      EmptyState {
        visible: root.items.length === 0
        anchors.centerIn: parent
        width: parent.width
        size: "overlay"
        glyph: root.emptyGlyph
        title: root.query !== "" ? root.noMatchesText.arg(root.query) : root.emptyText
      }
    }
  }
}
