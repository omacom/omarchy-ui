import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// A time ruler over one or more clip tracks, with a playhead (omareel's Timeline, ClipTrack and
// ZoomTrack, re-expressed in the kit's tokens).
//
//  - Ruler: labeled major ticks (`caption`, textDim) at a step that keeps labels about
//    `space(90)` apart, and four minor ticks between them (textFaint).
//  - Tracks: lanes in the normal fill, sharing the height under the ruler. Clips are foreground
//    glass blocks with a `bodySmall` label; the selected clip takes the selected fill, an accent
//    border and a bold label, plus a TrimHandle on each end.
//  - Playhead: an accent line with a small head on the ruler.
//
// Press or drag on the ruler or an empty lane to scrub (`seek`), press a clip to select it
// (`clipPressed`), drag a trim handle or nudge it with the arrow keys to trim (`trimmed`), and turn the wheel to zoom around the
// pointer (`viewChanged`). Left/Right move the playhead by a second (Shift: a major step),
// Home/End jump to the ends.
//
// `tracks` is a list of tracks, each a list of clips `{ start, end, label }` in seconds.
Item {
  id: root

  property real duration: 30
  property real position: 0
  property var tracks: []
  property int selectedTrack: -1
  property int selectedClip: -1
  // 1 shows the whole duration; more zooms in. `viewStart` is the time at the left edge.
  property real zoom: 1
  property real viewStart: 0
  property real maxZoom: 64
  property bool hasCursor: false

  signal seek(real time)
  signal clipPressed(int track, int clip)
  signal trimmed(int track, int clip, string edge, real time)
  signal viewChanged(real zoom, real viewStart)

  readonly property real rulerHeight: Style.space(24)
  readonly property real laneGap: Style.spacing.xs
  readonly property real clipInset: Style.spacing.xxs
  readonly property real smallRadius: Math.min(4, Style.cornerRadius)
  readonly property real pixelsPerSecond: width * zoom / Math.max(0.001, duration)
  readonly property real laneHeight: tracks.length > 0
    ? Math.floor((height - rulerHeight - laneGap * (tracks.length - 1)) / tracks.length) : 0
  readonly property real tickStep: {
    var target = Style.space(90) / Math.max(0.001, pixelsPerSecond)
    var steps = [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600]
    for (var i = 0; i < steps.length; i++) if (steps[i] >= target) return steps[i]
    return 3600
  }
  readonly property bool hot: hasCursor || activeFocus || playheadArea.containsMouse || playheadArea.pressed

  function xAt(time) { return Math.round((time - viewStart) * pixelsPerSecond) }
  function timeAt(x) { return Math.max(0, Math.min(duration, viewStart + x / pixelsPerSecond)) }
  function laneY(index) { return rulerHeight + index * (laneHeight + laneGap) }
  function format(seconds) {
    var s = Math.max(0, Math.round(seconds * 10) / 10)
    var m = Math.floor(s / 60)
    var rest = s - m * 60
    var whole = Math.floor(rest)
    var text = m + ":" + (whole < 10 ? "0" : "") + whole
    if (tickStep < 1) text += "." + Math.round((rest - whole) * 10)
    return text
  }
  function clampView(z, start) {
    z = Math.max(1, Math.min(maxZoom, z))
    var visible = duration / z
    return { zoom: z, start: Math.max(0, Math.min(duration - visible, start)) }
  }

  implicitWidth: Style.space(720)
  implicitHeight: Style.space(160)
  clip: true
  activeFocusOnTab: true

  // ---- ruler

  Item {
    id: ruler
    width: root.width
    height: root.rulerHeight

    Repeater {
      model: {
        var out = []
        var minor = root.tickStep / 5
        var first = Math.max(0, Math.floor(root.viewStart / minor))
        var last = Math.ceil((root.viewStart + root.duration / root.zoom) / minor)
        for (var i = first; i <= last; i++) {
          var t = i * minor
          if (t > root.duration + 1e-6) break
          out.push({ time: t, major: i % 5 === 0 })
        }
        return out
      }
      delegate: Rectangle {
        required property var modelData
        x: Math.min(root.width - width, root.xAt(modelData.time))
        y: ruler.height - height
        width: 1
        height: modelData.major ? Style.space(6) : Style.space(3)
        color: Commons.Color.textFaint
      }
    }

    Repeater {
      model: {
        var out = []
        var first = Math.max(0, Math.ceil(root.viewStart / root.tickStep - 1e-6))
        var last = Math.floor(Math.min(root.duration, root.viewStart + root.duration / root.zoom) / root.tickStep + 1e-6)
        for (var i = first; i <= last; i++) out.push(i * root.tickStep)
        return out
      }
      delegate: Text {
        required property var modelData
        // Centered on its tick, kept inside the ruler at both ends.
        x: Math.max(0, Math.min(root.width - width, root.xAt(modelData) - Math.round(width / 2)))
        y: Style.spacing.xxs
        text: root.format(modelData)
        color: Commons.Color.textDim
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
      }
    }
  }

  // ---- lanes and clips

  MouseArea {
    id: scrubArea
    anchors.fill: parent
    preventStealing: true
    onPressed: function(mouse) { root.forceActiveFocus(); root.seek(root.timeAt(mouse.x)) }
    onPositionChanged: function(mouse) { if (pressed) root.seek(root.timeAt(mouse.x)) }
    onWheel: function(wheel) {
      var steps = wheel.angleDelta.y / 120
      if (steps === 0) return
      var anchor = root.timeAt(wheel.x)
      var z = root.clampView(root.zoom * Math.pow(1.25, steps), 0).zoom
      var next = root.clampView(z, anchor - wheel.x / (root.width * z / root.duration))
      root.zoom = next.zoom
      root.viewStart = next.start
      root.viewChanged(next.zoom, next.start)
    }
  }

  Repeater {
    model: root.tracks
    delegate: Item {
      id: lane
      required property var modelData
      required property int index
      readonly property int trackIndex: index
      x: 0
      y: root.laneY(index)
      width: root.width
      height: root.laneHeight

      Rectangle {
        anchors.fill: parent
        radius: root.smallRadius
        color: Style.normalFill
      }

      Repeater {
        model: lane.modelData
        delegate: Item {
          id: clipItem
          required property var modelData
          required property int index
          readonly property bool selected: root.selectedTrack === lane.trackIndex && root.selectedClip === index
          readonly property var _spec: selected
            ? Border.flat(Commons.Color.accent, Math.max(1, Style.space(2)))
            : Border.controlSpec(clipArea.containsMouse ? "hover-cursor" : "normal", Commons.Color.foreground, Commons.Color.accent)
          x: root.xAt(modelData.start)
          y: root.clipInset
          width: root.xAt(modelData.end) - x
          height: lane.height - root.clipInset * 2

          BorderSurface {
            anchors.fill: parent
            radius: root.smallRadius
            color: clipItem.selected ? Style.selectedFill : (clipArea.containsMouse ? Style.hoverFill : Style.normalFill)
            borderSpec: clipItem._spec
          }

          readonly property real _labelInset: clipItem.selected ? Style.space(8) + Style.spacing.md : Style.spacing.controlPaddingX

          Text {
            x: clipItem._labelInset
            width: Math.max(0, parent.width - clipItem._labelInset * 2)
            anchors.verticalCenter: parent.verticalCenter
            text: clipItem.modelData.label || ""
            elide: Text.ElideRight
            color: Commons.Color.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.bodySmall
            font.bold: clipItem.selected
          }

          MouseArea {
            id: clipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: function(mouse) {
              root.forceActiveFocus()
              root.clipPressed(lane.trackIndex, clipItem.index)
            }
          }

          Repeater {
            model: clipItem.selected ? ["left", "right"] : []
            delegate: TrimHandle {
              id: handle
              required property string modelData
              property real origin: 0
              edge: modelData
              x: modelData === "left" ? 0 : clipItem.width - width
              height: clipItem.height
              onDraggingChanged: if (dragging) origin = modelData === "left" ? clipItem.modelData.start : clipItem.modelData.end
              onMoved: function(dx, dy) {
                root.trimmed(lane.trackIndex, clipItem.index, modelData,
                  Math.max(0, Math.min(root.duration, origin + dx / root.pixelsPerSecond)))
              }
              onNudged: function(dx, dy) {
                var at = modelData === "left" ? clipItem.modelData.start : clipItem.modelData.end
                root.trimmed(lane.trackIndex, clipItem.index, modelData,
                  Math.max(0, Math.min(root.duration, at + dx / root.pixelsPerSecond)))
              }
            }
          }
        }
      }
    }
  }

  // ---- playhead

  Item {
    id: playhead
    readonly property real lineWidth: Math.max(1, Style.space(2))
    readonly property real headWidth: Style.space(10)
    readonly property real headHeight: Style.space(10)
    readonly property color drawColor: root.hot ? Qt.tint(Commons.Color.accent, Qt.alpha(Commons.Color.foreground, 0.25)) : Commons.Color.accent
    x: root.xAt(root.position) - Math.round(headWidth / 2)
    y: root.rulerHeight - headHeight
    width: headWidth
    height: root.height - y
    visible: root.position >= root.viewStart && root.position <= root.viewStart + root.duration / root.zoom

    Rectangle {
      width: playhead.headWidth
      height: playhead.headHeight
      radius: root.smallRadius
      color: playhead.drawColor
    }
    Rectangle {
      x: Math.round((playhead.headWidth - playhead.lineWidth) / 2)
      y: playhead.headHeight
      width: playhead.lineWidth
      height: playhead.height - playhead.headHeight
      color: playhead.drawColor
    }
    MouseArea {
      id: playheadArea
      x: -Style.spacing.sm
      width: parent.width + Style.spacing.sm * 2
      height: playhead.headHeight
      hoverEnabled: true
      preventStealing: true
      cursorShape: Qt.SizeHorCursor
      onPressed: root.forceActiveFocus()
      onPositionChanged: function(mouse) {
        if (pressed) root.seek(root.timeAt(mapToItem(root, mouse.x, mouse.y).x))
      }
    }
  }

  Keys.onPressed: function(event) {
    var step = (event.modifiers & Qt.ShiftModifier) ? root.tickStep : 1
    if (event.key === Qt.Key_Left) root.seek(Math.max(0, root.position - step))
    else if (event.key === Qt.Key_Right) root.seek(Math.min(root.duration, root.position + step))
    else if (event.key === Qt.Key_Home) root.seek(0)
    else if (event.key === Qt.Key_End) root.seek(root.duration)
    else return
    event.accepted = true
  }
}
