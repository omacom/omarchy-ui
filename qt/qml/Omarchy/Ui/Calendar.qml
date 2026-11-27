import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A month grid, after the shell clock panel's: month and year between
// previous/next IconButtons, a dim caption row of weekdays, then six weeks of
// days (always six, so the grid is as tall in February as in August). Weeks
// start on Monday. Today is ringed in the accent, the selected day takes the
// selected fill, days outside the month are faint and weekends a little dim.
//
// Dates are "YYYY-MM-DD" strings, so nothing depends on the time zone and a
// still render never reads the clock: the app passes `today`. `month` is 1–12.
// The previous/next buttons and PageUp/PageDown step `year`/`month`; arrows
// move `cursorDate` (hover fill, the pointer moves it too) and Return/Space
// pick it, as a click does: `selected` takes the day and `picked(date)` fires.
FocusScope {
  id: root

  property int year: 2026
  property int month: 1
  property string today: ""
  property string selected: ""
  property string cursorDate: ""
  // Force the hover look on `cursorDate` without focus, for still renders.
  property bool hasCursor: false

  property var monthNames: ["January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"]
  property var weekdayNames: ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]

  property color foreground: Commons.Color.foreground
  property color accent: Commons.Color.accent

  signal picked(string date)

  readonly property int cellWidth: Style.space(36)
  readonly property int cellHeight: Style.spacing.controlHeight
  readonly property int cellSpacing: Style.spacing.xxs
  readonly property int gridWidth: cellWidth * 7 + cellSpacing * 6
  readonly property bool _showCursor: hasCursor || activeFocus || _hovering
  property bool _hovering: false

  function pad(n) { return (n < 10 ? "0" : "") + n }
  function iso(y, m, d) { return y + "-" + pad(m) + "-" + pad(d) }
  // Noon, so no date lands on a missing midnight.
  function dateOf(s) {
    var p = String(s).split("-")
    return new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2]), 12)
  }
  function isoOf(d) { return iso(d.getFullYear(), d.getMonth() + 1, d.getDate()) }

  // 42 days from the Monday on or before the 1st.
  readonly property var days: {
    var first = new Date(year, month - 1, 1, 12)
    var back = (first.getDay() + 6) % 7
    var out = []
    for (var i = 0; i < 42; i++) {
      var d = new Date(year, month - 1, 1 - back + i, 12)
      out.push({
        date: isoOf(d),
        day: d.getDate(),
        inMonth: d.getMonth() === month - 1,
        weekend: i % 7 >= 5
      })
    }
    return out
  }

  function stepMonth(delta) {
    var m = (month - 1) + delta
    year += Math.floor(m / 12)
    month = ((m % 12) + 12) % 12 + 1
  }

  function moveCursor(days) {
    var from = cursorDate !== "" ? cursorDate : (selected !== "" ? selected : (today !== "" ? today : iso(year, month, 1)))
    var d = dateOf(from)
    d.setDate(d.getDate() + days)
    cursorDate = isoOf(d)
    if (d.getFullYear() !== year || d.getMonth() + 1 !== month) {
      year = d.getFullYear()
      month = d.getMonth() + 1
    }
  }

  function pick(date) {
    selected = date
    picked(date)
  }

  implicitWidth: gridWidth
  implicitHeight: column.implicitHeight
  activeFocusOnTab: true

  Keys.onLeftPressed: moveCursor(-1)
  Keys.onRightPressed: moveCursor(1)
  Keys.onUpPressed: moveCursor(-7)
  Keys.onDownPressed: moveCursor(7)
  Keys.onPressed: function(event) {
    if (event.key === Qt.Key_PageUp) { stepMonth(-1); event.accepted = true }
    else if (event.key === Qt.Key_PageDown) { stepMonth(1); event.accepted = true }
  }
  Keys.onReturnPressed: if (cursorDate !== "") pick(cursorDate)
  Keys.onEnterPressed: if (cursorDate !== "") pick(cursorDate)
  Keys.onSpacePressed: if (cursorDate !== "") pick(cursorDate)

  Column {
    id: column
    width: root.gridWidth
    spacing: Style.spacing.sm

    // ---- Month and year between the steppers.
    Item {
      width: parent.width
      height: prev.height

      Text {
        width: parent.width
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: (root.monthNames[root.month - 1] + " " + root.year).toUpperCase()
        color: Commons.Color.textDim
        font.family: Style.font.family
        font.pixelSize: Style.font.body
      }

      PanelActionButton {
        id: prev
        x: Math.round((root.cellWidth - width) / 2)
        iconText: "󰅁"
        tooltipText: Commons.I18n.t("clock.previousMonth", "Previous month")
        foreground: root.foreground
        onClicked: root.stepMonth(-1)
      }

      PanelActionButton {
        x: root.width - root.cellWidth + Math.round((root.cellWidth - width) / 2)
        iconText: "󰅂"
        tooltipText: Commons.I18n.t("clock.nextMonth", "Next month")
        foreground: root.foreground
        onClicked: root.stepMonth(1)
      }
    }

    Column {
      spacing: root.cellSpacing

      // ---- Weekdays
      Row {
        spacing: root.cellSpacing
        Repeater {
          model: root.weekdayNames
          Text {
            required property var modelData
            width: root.cellWidth
            height: Style.space(16)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.PlainText
            text: String(modelData).toUpperCase()
            color: Commons.Color.textDim
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            font.bold: true
          }
        }
      }

      // ---- Six weeks
      Repeater {
        model: 6
        Row {
          id: week
          required property int index
          spacing: root.cellSpacing

          Repeater {
            model: 7
            Rectangle {
              id: cell
              required property int index
              readonly property var info: root.days[week.index * 7 + index]
              readonly property bool isToday: info.date === root.today
              readonly property bool isSelected: info.date === root.selected
              readonly property bool isCursor: root._showCursor && info.date === root.cursorDate

              width: root.cellWidth
              height: root.cellHeight
              radius: Style.cornerRadius
              color: isCursor
                ? Style.hoverFillFor(root.foreground, root.accent)
                : (isSelected ? Style.selectedFillFor(root.foreground, root.accent) : "transparent")
              border.width: isToday ? Style.spacing.hairline : 0
              border.color: root.accent

              Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: cell.info.day
                color: !cell.info.inMonth
                  ? Commons.Color.textFaint
                  : (cell.isSelected
                    ? Style.selectedStateColor(root.foreground, root.accent)
                    : (cell.info.weekend ? Commons.Color.textDim : root.foreground))
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                font.bold: cell.isSelected || cell.isToday
              }

              MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: { root._hovering = true; root.cursorDate = cell.info.date }
                onExited: root._hovering = false
                onClicked: {
                  root.forceActiveFocus()
                  root.pick(cell.info.date)
                }
              }
            }
          }
        }
      }
    }
  }
}
