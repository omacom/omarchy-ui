import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Window
import Omarchy.Theme
import Omarchy.Theme as Commons

// Searchable multi-select dropdown. Trigger shape matches Dropdown /
// SearchableDropdown; the popup shows a search field, an optional refresh
// button, and a checkbox list. Click rows to toggle. Use when callers
// need to pick zero or more items from a (possibly long, possibly
// dynamic) list.
//
// Options are a plain model: `options`, string[] or
// [{ value, label, description? }]. The shell's command-backed loading
// (`optionsCommand`, a Quickshell Process) is dropped here: an app that
// loads options itself sets `loadingOptions` / `optionsError` while it
// does, and sets `refreshable` to show the refresh button, which emits
// `refreshRequested()`.
//
// `values` is the persisted selection — always an array of strings.
// Emits `changed(values)` whenever the selection mutates.
Item {
  id: root

  property string label: ""
  property var values: []
  property var options: []
  property bool refreshable: false
  property string placeholderText: Commons.I18n.t("common.search")
  property string emptyText: Commons.I18n.t("common.noOptions")
  property string noSelectionText: Commons.I18n.t("common.noneSelected")
  property string triggerLabel: ""
  property bool showLabel: true

  property color foreground: Commons.Color.popups.text
  property color background: Commons.Color.popups.background
  property color popupBorder: Commons.Color.popups.border
  property color accent: Commons.Color.accent
  readonly property var popupBorderSpec: Border.localOrSurfaceSpec("popups", "border", popupBorder, Commons.Color.popups.border, Style.normalBorderWidth)
  property string fontFamily: Style.font.family
  property int rowHeight: Style.spacing.controlHeight
  property int popupRowHeight: Style.spacing.popupRowHeight
  property int popupMinHeight: Style.spacing.searchablePopupMinHeight

  property bool hasCursor: false

  readonly property bool popupOpen: popup.opened
  function open() { popup.open() }
  function close() { popup.close() }
  function toggle() { popup.opened ? popup.close() : popup.open() }

  signal changed(var values)
  signal hovered(bool isHovered)
  signal refreshRequested()

  // `options`, normalized into the [{ value, label, description }] shape
  // for delegate use.
  property var resolvedOptions: []
  property bool loadingOptions: false
  property string optionsError: ""

  function normalizeOption(o) {
    if (o && typeof o === "object") {
      return {
        value: String(o.value),
        label: String(o.label !== undefined ? o.label : o.value),
        description: o.description ? String(o.description) : ""
      }
    }
    var s = String(o)
    return { value: s, label: s, description: "" }
  }

  // QML schema arrays sometimes arrive as JSValue lists that fail
  // `Array.isArray`. arrayFrom accepts anything array-like (`.length`
  // numeric) and returns a real JS array so the rest of the component can
  // rely on standard array operations.
  function arrayFrom(v) {
    if (!v || typeof v.length !== "number" || typeof v === "string") return []
    var out = []
    for (var i = 0; i < v.length; i++) out.push(v[i])
    return out
  }

  function normalizeAll(arr) {
    var out = []
    var src = arrayFrom(arr)
    var seen = ({})
    for (var i = 0; i < src.length; i++) {
      var n = normalizeOption(src[i])
      if (!n.value) continue
      if (seen[n.value]) continue
      seen[n.value] = true
      out.push(n)
    }
    return out
  }

  function valueSet() {
    var set = ({})
    var arr = arrayFrom(values)
    for (var i = 0; i < arr.length; i++) set[String(arr[i])] = true
    return set
  }

  function isSelected(value) {
    var set = valueSet()
    return !!set[String(value)]
  }

  function toggleValue(value) {
    var v = String(value)
    var arr = arrayFrom(values)
    var idx = arr.indexOf(v)
    if (idx === -1) arr.push(v)
    else arr.splice(idx, 1)
    root.values = arr
    root.changed(arr)
  }

  function selectionLabel() {
    var arr = arrayFrom(values)
    if (arr.length === 0) return ""
    var labels = []
    var byValue = ({})
    for (var i = 0; i < resolvedOptions.length; i++)
      byValue[resolvedOptions[i].value] = resolvedOptions[i].label
    for (var j = 0; j < arr.length; j++)
      labels.push(byValue[String(arr[j])] || String(arr[j]))
    if (labels.length <= 3) return labels.join(", ")
    return Commons.I18n.tn("multiSelect.selectedCount", arr.length).arg(Commons.I18n.formatNumber(arr.length))
  }

  property var filtered: resolvedOptions
  function recomputeFiltered() {
    var q = searchField.text.toLowerCase()
    if (!q) { filtered = resolvedOptions; return }
    var out = []
    for (var i = 0; i < resolvedOptions.length; i++) {
      var o = resolvedOptions[i]
      if (o.label.toLowerCase().indexOf(q) !== -1
          || o.description.toLowerCase().indexOf(q) !== -1
          || o.value.toLowerCase().indexOf(q) !== -1) out.push(o)
    }
    filtered = out
  }

  function rebuildFromStatic() {
    resolvedOptions = normalizeAll(options)
    recomputeFiltered()
  }

  function refresh() {
    rebuildFromStatic()
  }

  onOptionsChanged: rebuildFromStatic()
  Component.onCompleted: refresh()

  implicitWidth: Style.spacing.searchableDropdownWidth
  implicitHeight: col.implicitHeight

  Column {
    id: col
    anchors.fill: parent
    spacing: Style.spacing.labelGap

    Text {
      textFormat: Text.PlainText
      visible: root.showLabel && root.label !== ""
      text: root.label
      color: Commons.Color.textDim
      font.family: root.fontFamily
      font.pixelSize: Style.font.caption
      font.bold: true
    }

    BorderSurface {
      id: trigger
      width: parent.width
      height: root.rowHeight
      radius: Style.cornerRadius

      readonly property bool _focused: trigger.activeFocus
      readonly property bool _hot: triggerHover.hovered || root.hasCursor
      readonly property var _borderSpec: Border.controlSpec(trigger._focused ? "focus" : (trigger._hot ? "hover-cursor" : "normal"), root.foreground, root.accent)

      color: Style.controlFill(trigger._focused, trigger._hot, root.foreground, root.accent)
      borderSpec: _borderSpec

      activeFocusOnTab: true

      HoverHandler {
        id: triggerHover
        onHoveredChanged: root.hovered(hovered)
      }

      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            || event.key === Qt.Key_Space || event.key === Qt.Key_Down || event.text === "j") {
          popup.opened ? popup.close() : popup.open()
          event.accepted = true
        } else if (event.key === Qt.Key_Escape && popup.opened) {
          popup.close(); event.accepted = true
        }
      }

      Text {
        textFormat: Text.PlainText
        anchors.left: parent.left
        anchors.right: chevron.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: trigger.borderLeft + Style.spacing.controlPaddingX
        anchors.rightMargin: Style.spacing.md
        text: root.selectionLabel() || root.triggerLabel || root.noSelectionText
        color: root.selectionLabel() ? root.foreground : Commons.Color.textDim
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
        elide: Text.ElideRight
      }

      Text {
        id: chevron
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: trigger.borderRight + Style.spacing.controlGap
        text: "󰅀"
        color: Commons.Color.textDim
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
      }

      MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        // CloseOnPressOutsideParent treats a press on the trigger as inside,
        // so the popup is still open here and a second click collapses it
        // instead of reopening.
        onClicked: {
          trigger.forceActiveFocus()
          popup.opened ? popup.close() : popup.open()
        }
      }

      QQC.Popup {
        id: popup
        closePolicy: QQC.Popup.CloseOnPressOutsideParent | QQC.Popup.CloseOnEscape
        // Keep the trigger as the logical parent so CloseOnPressOutsideParent
        // treats a press on the trigger as inside, leaving the toggle to the
        // trigger's onClicked. The popup item is still rendered in the window
        // overlay, so it is not clipped by the trigger's ancestors. Position
        // and available height are recomputed on open and any time the
        // trigger's geometry changes, since a binding on mapToItem alone
        // won't reliably re-evaluate when ancestors scroll or resize.
        parent: trigger
        property real _anchorX: 0
        property real _anchorY: 0
        property real _availableBelow: 0
        readonly property var _windowItem: trigger.Window.window ? trigger.Window.window.contentItem : null
        readonly property real _windowHeight: _windowItem ? _windowItem.height : 0
        readonly property real _chromeHeight: searchHeader.height + 1 + topPadding + bottomPadding
        readonly property real _idealContent: resultList.contentHeight + _chromeHeight
        readonly property real _maxRowsHeight: root.popupRowHeight * 6 + 5 * Style.spacing.labelGap + _chromeHeight

        function reposition() {
          if (!parent) return
          var p = trigger.mapToItem(parent, 0, trigger.height + Style.spacing.xxs)
          _anchorX = p.x
          _anchorY = p.y
          // _anchorY is relative to the trigger; the clamp below needs the
          // anchor in window coordinates.
          var win = _windowItem ? trigger.mapToItem(_windowItem, 0, trigger.height + Style.spacing.xxs) : p
          _availableBelow = Math.max(0, _windowHeight - win.y - Style.space(12))
        }

        x: _anchorX
        y: _anchorY
        width: trigger.width
        // Clamp to whatever fits below the trigger; don't force popupMinHeight
        // when there isn't room, otherwise the popup overflows the window.
        implicitHeight: Math.min(_availableBelow, _idealContent, _maxRowsHeight)
        padding: Style.spacing.hairline
        leftPadding: Border.left(root.popupBorderSpec) + Style.spacing.hairline
        rightPadding: Border.right(root.popupBorderSpec) + Style.spacing.hairline
        topPadding: Border.top(root.popupBorderSpec) + Style.spacing.hairline
        bottomPadding: Border.bottom(root.popupBorderSpec) + Style.spacing.hairline
        focus: true

        Connections {
          target: trigger
          function onXChanged() { popup.reposition() }
          function onYChanged() { popup.reposition() }
          function onWidthChanged() { popup.reposition() }
          function onHeightChanged() { popup.reposition() }
        }

        background: BorderSurface {
          color: root.background
          borderSpec: root.popupBorderSpec
          radius: Style.cornerRadius
        }

        onOpened: {
          reposition()
          searchField.text = ""
          root.refresh()
          root.recomputeFiltered()
          Qt.callLater(function() { searchField.forceActiveFocus() })
        }
        onClosed: searchField.text = ""

        contentItem: Column {
          spacing: 0

          Item {
            id: searchHeader
            width: parent.width
            height: root.popupRowHeight + Style.spacing.controlPaddingX

            Row {
              anchors.fill: parent
              anchors.margins: Style.spacing.md
              spacing: Style.spacing.rowGap

              TextField {
                id: searchField
                width: parent.width - (refreshButton.visible ? refreshButton.width + parent.spacing : 0)
                height: parent.height
                placeholderText: root.placeholderText
                foreground: root.foreground
                accent: root.accent
                font.family: root.fontFamily
                font.pixelSize: Style.font.body

                onTextChanged: {
                  root.recomputeFiltered()
                  if (resultList.count > 0) resultList.currentIndex = 0
                }

                Keys.onPressed: function(event) {
                  if (event.key === Qt.Key_Escape) {
                    popup.close(); event.accepted = true
                  } else if (event.key === Qt.Key_Down) {
                    if (resultList.count > 0) {
                      resultList.currentIndex = 0
                      resultList.forceActiveFocus()
                    }
                    event.accepted = true
                  } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (resultList.count > 0) {
                      resultList.currentIndex = 0
                      resultList.toggleCurrent()
                    }
                    event.accepted = true
                  }
                }
              }

              BorderSurface {
                id: refreshButton
                visible: root.refreshable
                enabled: !root.loadingOptions
                width: parent.height
                height: parent.height
                radius: Style.cornerRadius
                color: refreshHover.hovered
                  ? Style.hoverFillFor(root.foreground, root.accent)
                  : Style.normalFillFor(root.foreground, root.accent)
                borderSpec: refreshHover.hovered
                  ? Border.controlSpec("hover-cursor", root.foreground, root.accent)
                  : Border.controlSpec("normal", root.foreground, root.accent)

                Text {
                  textFormat: Text.PlainText
                  anchors.centerIn: parent
                  text: root.loadingOptions ? "󰦖" : "󰑐"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body

                  RotationAnimator on rotation {
                    running: root.loadingOptions && !Style.reduceMotion
                    from: 0; to: 360
                    duration: Style.duration(800)
                    loops: Animation.Infinite
                  }
                }

                HoverHandler { id: refreshHover }
                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.refreshRequested()
                }
              }
            }
          }

          Rectangle {
            width: parent.width
            height: 1
            color: Util.alpha(root.foreground, 0.10)
          }

          Item {
            width: parent.width
            height: Math.max(0, popup.availableHeight - searchHeader.height - 1)

            Text {
              textFormat: Text.PlainText
              anchors.centerIn: parent
              visible: resultList.count === 0
              text: root.loadingOptions ? Commons.I18n.t("common.loading") : (root.optionsError !== "" ? root.optionsError : root.emptyText)
              color: Commons.Color.textFaint
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
            }

            ListView {
              id: resultList
              anchors.fill: parent
              spacing: Style.spacing.labelGap
              clip: true
              boundsBehavior: Flickable.StopAtBounds
              model: root.filtered
              currentIndex: -1
              keyNavigationEnabled: false

              function toggleCurrent() {
                if (currentIndex < 0 || currentIndex >= root.filtered.length) return
                root.toggleValue(root.filtered[currentIndex].value)
              }

              Keys.priority: Keys.BeforeItem
              Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                  popup.close(); event.accepted = true
                } else if (event.key === Qt.Key_Down || event.text === "j") {
                  if (resultList.currentIndex >= resultList.count - 1) {
                    event.accepted = true; return
                  }
                  resultList.currentIndex = resultList.currentIndex + 1
                  event.accepted = true
                } else if (event.key === Qt.Key_Up || event.text === "k") {
                  if (resultList.currentIndex <= 0) {
                    searchField.forceActiveFocus()
                    event.accepted = true; return
                  }
                  resultList.currentIndex = resultList.currentIndex - 1
                  event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                           || event.key === Qt.Key_Space) {
                  resultList.toggleCurrent(); event.accepted = true
                }
              }

              delegate: Rectangle {
                required property var modelData
                required property int index

                readonly property bool selected: root.isSelected(modelData.value)

                width: resultList.width
                height: Math.max(root.popupRowHeight, rowContent.implicitHeight + Style.spacing.rowPaddingX)
                radius: Math.min(4, Style.cornerRadius)
                color: index === resultList.currentIndex
                  ? Style.hoverFillFor(root.foreground, root.accent)
                  : "transparent"

                Row {
                  id: rowContent
                  anchors.left: parent.left
                  anchors.right: parent.right
                  anchors.verticalCenter: parent.verticalCenter
                  anchors.leftMargin: Style.spacing.controlPaddingX
                  anchors.rightMargin: Style.spacing.controlPaddingX
                  spacing: Style.spacing.rowGap

                  BorderSurface {
                    id: checkbox
                    width: Style.space(16)
                    height: Style.space(16)
                    radius: Math.min(4, Style.cornerRadius)
                    anchors.verticalCenter: parent.verticalCenter
                    color: selected ? Style.selectedFillFor(root.foreground, root.accent) : "transparent"
                    borderSpec: selected
                      ? Border.controlSpec("selected", root.foreground, root.accent)
                      : Border.controlSpec("normal", root.foreground, root.accent)

                    Text {
                      anchors.centerIn: parent
                      visible: selected
                      text: "✓"
                      color: Style.selectedStateColor(root.foreground, root.accent)
                      font.family: root.fontFamily
                      font.pixelSize: Math.round(checkbox.height * 0.85)
                      font.bold: true
                    }
                  }

                  Column {
                    width: parent.width - checkbox.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Style.spacing.xxs

                    Text {
                      textFormat: Text.PlainText
                      text: modelData.label
                      color: index === resultList.currentIndex ? Style.hoverStateColor(root.foreground, root.accent) : root.foreground
                      font.family: root.fontFamily
                      font.pixelSize: Style.font.body
                      elide: Text.ElideRight
                      width: parent.width
                    }
                    Text {
                      textFormat: Text.PlainText
                      visible: text !== ""
                      text: modelData.description
                      color: Commons.Color.textDim
                      font.family: root.fontFamily
                      font.pixelSize: Style.font.caption
                      elide: Text.ElideRight
                      width: parent.width
                    }
                  }
                }

                MouseArea {
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  onPositionChanged: resultList.currentIndex = parent.index
                  onClicked: root.toggleValue(modelData.value)
                }
              }
            }
          }
        }
      }
    }
  }
}
