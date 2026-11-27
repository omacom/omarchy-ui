import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A media transport scrub: elapsed time, a one-track position bar, total time. The bar is the
// Slider's (same track height, knob and ring), with the unplayed part in `track` and the played
// part in the foreground. Times are `bodySmall` textDim, m:ss (h:mm:ss from an hour).
//
// Press or drag the bar to `seek`; Left/Right step 5 s (Shift: 30 s), Home/End jump to the ends.
Item {
  id: root

  property real position: 0
  property real duration: 0
  property bool hasCursor: false
  // The knob's ring: the surface the scrubber sits on.
  property color surfaceColor: Commons.Color.popups.background

  signal seek(real time)

  readonly property real trackHeight: Math.max(4, Math.round(Style.spacing.controlHeight * 0.11))
  readonly property real knobSize: Math.max(14, Math.round(Style.spacing.controlHeight * 0.38))
  readonly property real progress: duration > 0 ? Math.max(0, Math.min(1, position / duration)) : 0
  readonly property bool hot: hasCursor || activeFocus || area.containsMouse || area.pressed

  function format(seconds) {
    var s = Math.max(0, Math.floor(seconds))
    var h = Math.floor(s / 3600)
    var m = Math.floor((s % 3600) / 60)
    var sec = s % 60
    var pad = function(n) { return (n < 10 ? "0" : "") + n }
    return h > 0 ? h + ":" + pad(m) + ":" + pad(sec) : m + ":" + pad(sec)
  }

  implicitWidth: Style.space(360)
  implicitHeight: Math.max(Style.space(22), knobSize + Style.spacing.md)
  activeFocusOnTab: true

  Text {
    id: elapsed
    anchors.verticalCenter: parent.verticalCenter
    text: root.format(root.position)
    color: Commons.Color.textDim
    font.family: Style.font.family
    font.pixelSize: Style.font.bodySmall
  }

  Text {
    id: total
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    text: root.format(root.duration)
    color: Commons.Color.textDim
    font.family: Style.font.family
    font.pixelSize: Style.font.bodySmall
  }

  Item {
    id: bar
    x: elapsed.width + Style.spacing.controlGap
    width: total.x - Style.spacing.controlGap - x
    height: root.height

    readonly property real _ring: Math.max(1, Style.space(2))
    readonly property real _innerKnob: Math.max(4, root.knobSize - _ring * 2)
    readonly property real _knobCenter: track.x + Math.round(track.width * root.progress)

    Item {
      id: track
      x: root.knobSize / 2
      width: bar.width - root.knobSize
      height: root.trackHeight
      anchors.verticalCenter: parent.verticalCenter
    }
    Rectangle {
      readonly property real _w: Math.max(0, Math.min(track.width, bar._knobCenter - root.knobSize / 2 - track.x))
      x: track.x
      width: _w
      visible: _w > 0
      height: track.height
      anchors.verticalCenter: parent.verticalCenter
      radius: track.height / 2
      color: Commons.Color.foreground
    }
    Rectangle {
      readonly property real _start: Math.min(track.x + track.width, Math.max(track.x, bar._knobCenter + root.knobSize / 2))
      readonly property real _w: Math.max(0, track.x + track.width - _start)
      x: _start
      width: _w
      visible: _w > 0
      height: track.height
      anchors.verticalCenter: parent.verticalCenter
      radius: track.height / 2
      color: Commons.Color.track
    }
    Rectangle {
      width: bar._innerKnob
      height: bar._innerKnob
      radius: width / 2
      x: bar._knobCenter - width / 2
      anchors.verticalCenter: parent.verticalCenter
      color: Commons.Color.foreground
      scale: root.hot ? 1.15 : 1.0
      Behavior on scale { NumberAnimation { duration: Style.duration(110); easing.type: Easing.OutCubic } }
    }

    MouseArea {
      id: area
      anchors.fill: parent
      hoverEnabled: true
      preventStealing: true
      cursorShape: Qt.PointingHandCursor
      function timeAt(x) {
        if (track.width <= 0) return 0
        return Math.max(0, Math.min(1, (x - track.x) / track.width)) * root.duration
      }
      onPressed: function(mouse) { root.forceActiveFocus(); root.seek(timeAt(mouse.x)) }
      onPositionChanged: function(mouse) { if (pressed) root.seek(timeAt(mouse.x)) }
    }
  }

  Keys.onPressed: function(event) {
    var step = (event.modifiers & Qt.ShiftModifier) ? 30 : 5
    if (event.key === Qt.Key_Left) root.seek(Math.max(0, root.position - step))
    else if (event.key === Qt.Key_Right) root.seek(Math.min(root.duration, root.position + step))
    else if (event.key === Qt.Key_Home) root.seek(0)
    else if (event.key === Qt.Key_End) root.seek(root.duration)
    else return
    event.accepted = true
  }
}
