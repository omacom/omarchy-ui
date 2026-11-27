import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

Item {
  id: root

  property QtObject bar: null
  property real value: 0
  property real minimum: 0
  property real maximum: 1
  property real step: 0.05
  property bool integer: false
  property color trackColor: bar ? Style.selectedFillFor(bar.foreground, Commons.Color.accent) : Style.selectedFillFor(Commons.Color.foreground, Commons.Color.accent)
  property color fillColor: bar ? bar.foreground : Commons.Color.foreground
  property color knobColor: bar ? bar.foreground : Commons.Color.foreground
  property bool dragging: false
  property real trackHeight: Math.max(4, Math.round(Style.spacing.controlHeight * 0.11))
  property real knobSize: Math.max(14, Math.round(Style.spacing.controlHeight * 0.38))
  property real liveValue: value

  // macOS-style notches. When > 1, that many evenly-spaced tick marks are cut
  // into the track (drawn in the panel background color, so only the part
  // crossing the track shows). Purely visual — snapping is the caller's job via
  // `integer`/`step` or an index-based value. Default 0 leaves the track plain.
  property int tickCount: 0
  property color tickColor: bar ? bar.background : Commons.Color.background

  onValueChanged: if (!dragging) liveValue = value

  signal moved(real value)
  signal released(real value)

  // Right-click is a secondary action on the whole track — audio uses it to
  // mute the channel the slider belongs to. Dragging stays left-button only.
  signal rightClicked()

  implicitWidth: Style.space(200)
  implicitHeight: Math.max(Style.space(22), knobSize + Style.spacing.md)

  readonly property real range: Math.max(0.0001, maximum - minimum)
  readonly property real progress: Math.max(0, Math.min(1, (liveValue - minimum) / range))
  readonly property bool _hot: mouseArea.containsMouse || root.dragging
  // In a mirrored (right-to-left) layout the anchored fill already starts at
  // the right; the knob, ticks, and pointer are placed by x, so they flip here.
  readonly property bool mirrored: LayoutMirroring.enabled
  function trackX(fraction) {
    return track.x + track.width * (root.mirrored ? 1 - fraction : fraction)
  }

  readonly property real _ring: Math.max(1, Style.space(2))
  readonly property real _innerKnob: Math.max(4, knobSize - _ring * 2)
  readonly property real _knobCenter: trackX(progress)

  Item {
    id: track
    anchors.verticalCenter: parent.verticalCenter
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.leftMargin: root.knobSize / 2
    anchors.rightMargin: root.knobSize / 2
    height: root.trackHeight
  }

  // Split the track around the knob with a true transparent gap of `_ring`, so
  // the slider sits cleanly on any surface (window, glass Pane, hovered row)
  // without painting an opaque background-colored cutout halo.
  Rectangle {
    id: fill
    readonly property real _end: root.mirrored
      ? track.x + track.width
      : Math.max(track.x, Math.min(track.x + track.width, root._knobCenter - root.knobSize / 2))
    readonly property real _start: root.mirrored
      ? Math.min(track.x + track.width, Math.max(track.x, root._knobCenter + root.knobSize / 2))
      : track.x
    anchors.verticalCenter: track.verticalCenter
    x: _start
    width: Math.max(0, _end - _start)
    visible: width > 0
    height: track.height
    radius: track.height / 2
    color: root.fillColor
  }

  Rectangle {
    id: unfilledTrack
    readonly property real _start: root.mirrored
      ? track.x
      : Math.min(track.x + track.width, Math.max(track.x, root._knobCenter + root.knobSize / 2))
    readonly property real _end: root.mirrored
      ? Math.max(track.x, Math.min(track.x + track.width, root._knobCenter - root.knobSize / 2))
      : track.x + track.width
    anchors.verticalCenter: track.verticalCenter
    x: _start
    width: Math.max(0, _end - _start)
    visible: width > 0
    height: track.height
    radius: track.height / 2
    color: root.trackColor
  }

  Repeater {
    model: root.tickCount > 2 ? root.tickCount - 2 : 0
    Rectangle {
      required property int index
      readonly property real _cx: root.trackX((index + 1) / (root.tickCount - 1))
      visible: Math.abs(_cx - root._knobCenter) > root.knobSize / 2
      width: Math.max(1, Style.space(2))
      height: root.trackHeight + Style.space(4)
      radius: 1
      color: root.tickColor
      anchors.verticalCenter: track.verticalCenter
      x: _cx - width / 2
    }
  }

  Rectangle {
    id: knob
    width: root._innerKnob
    height: root._innerKnob
    radius: width / 2
    color: root.knobColor
    anchors.verticalCenter: track.verticalCenter
    x: root._knobCenter - width / 2
    scale: root._hot ? 1.15 : 1.0

    Behavior on x {
      enabled: !root.dragging
      NumberAnimation { duration: Style.duration(140); easing.type: Easing.OutCubic }
    }

    Behavior on scale {
      NumberAnimation { duration: Style.duration(110); easing.type: Easing.OutCubic }
    }
  }

  MouseArea {
    id: mouseArea
    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    acceptedButtons: Qt.LeftButton | Qt.RightButton

    function valueFromX(x) {
      if (track.width <= 0) return root.minimum
      var fraction = Math.max(0, Math.min(1, (x - track.x) / track.width))
      if (root.mirrored) fraction = 1 - fraction
      var raw = root.minimum + fraction * root.range
      if (root.integer) raw = Math.round(raw)
      return Math.max(root.minimum, Math.min(root.maximum, raw))
    }

    onPressed: function(mouse) {
      if (mouse.button !== Qt.LeftButton) return
      root.dragging = true
      var next = valueFromX(mouse.x)
      root.liveValue = next
      root.moved(next)
    }
    onClicked: function(mouse) {
      if (mouse.button === Qt.RightButton) root.rightClicked()
    }
    onPositionChanged: function(mouse) {
      if (!root.dragging) return
      var next = valueFromX(mouse.x)
      root.liveValue = next
      root.moved(next)
    }
    onReleased: function(mouse) {
      if (mouse.button !== Qt.LeftButton) return
      root.dragging = false
      root.released(root.liveValue)
      root.liveValue = root.value
    }
    onWheel: function(wheel) {
      var delta = wheel.angleDelta.y > 0 ? root.step : -root.step
      var next = Math.max(root.minimum, Math.min(root.maximum, root.liveValue + delta))
      if (root.integer) next = Math.round(next)
      root.liveValue = next
      root.moved(next)
      root.released(next)
    }
  }
}
