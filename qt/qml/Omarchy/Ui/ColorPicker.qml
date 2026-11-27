import QtQuick
import QtQuick.Layouts
import Omarchy.Theme
import Omarchy.Theme as Commons
import Omarchy.Theme.Native

// An HSV color picker (omasnap's custom color panel, re-expressed in the kit's tokens):
//
//  - a saturation/value square (saturation left to right, value top to bottom) over the hue,
//  - a vertical hue strip and, with `showAlpha`, an alpha strip (opaque at the top),
//  - a preview swatch and a hex TextField (`#rrggbb`, or `#rrggbbaa` when alpha < 1),
//  - a row of the theme's palette colors (`paletteNames`, keys of colors.toml).
//
// Corners are `min(4, radius)`. Knobs are the Slider's: `max(14, controlHeight × 0.38)` round,
// ringed `space(2)` in the surface color; the square's knob is filled with the picked color.
// The color is `hue`, `saturation`, `value`, `alpha` (0–1); `setColor()` sets all four from a
// color or hex string, and every user change emits `edited(color)`.
//
// Keys: arrows move the square's knob (1 %, Shift 10 %), Up/Down move a focused strip.
Item {
  id: root

  property real hue: 0
  property real saturation: 0
  property real value: 1
  property real alpha: 1
  property bool showAlpha: true
  property bool hasCursor: false
  property color surfaceColor: Commons.Color.popups.background
  // Named colors from the theme's colors.toml, in this order; missing ones are skipped.
  property var paletteNames: ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"]

  signal edited(color color)

  readonly property var rgb: hsvToRgb(hue, saturation, value)
  readonly property color color: Qt.rgba(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255, alpha)
  readonly property string hex: {
    var out = "#" + hex2(rgb[0]) + hex2(rgb[1]) + hex2(rgb[2])
    return alpha < 1 ? out + hex2(Math.round(alpha * 255)) : out
  }
  readonly property real smallRadius: Math.min(4, Style.cornerRadius)
  readonly property real knobSize: Math.max(14, Math.round(Style.spacing.controlHeight * 0.38))
  readonly property real stripWidth: Style.space(14)
  readonly property real swatchSize: Style.space(20)

  // The theme's colors.toml, read the way Color.qml reads it.
  readonly property var namedColors: {
    var out = {}
    var lines = String(ThemeSource.colorsText || "").split("\n")
    for (var i = 0; i < lines.length; i++) {
      var m = lines[i].match(/^\s*([A-Za-z0-9_-]+)\s*=\s*["']?(#[0-9A-Fa-f]{6})/)
      if (m) out[m[1]] = m[2].toLowerCase()
    }
    if (!out.foreground) out.foreground = String(Commons.Color.foreground)
    if (!out.background) out.background = String(Commons.Color.background)
    return out
  }
  readonly property var swatches: {
    var out = []
    for (var i = 0; i < paletteNames.length; i++) if (namedColors[paletteNames[i]]) out.push(namedColors[paletteNames[i]])
    return out
  }

  function hex2(n) { return (n < 16 ? "0" : "") + n.toString(16) }
  // HSV (0–1) to 0–255 channels; the Rune binding does the same arithmetic.
  function hsvToRgb(h, s, v) {
    var h6 = (h - Math.floor(h)) * 6
    var i = Math.floor(h6)
    var f = h6 - i
    var p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f))
    var c = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6]
    return [Math.round(c[0] * 255), Math.round(c[1] * 255), Math.round(c[2] * 255)]
  }
  function setColor(c) {
    var s = String(c)
    var m = s.match(/^#?([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$/)
    if (!m) return false
    var r = parseInt(m[1].substr(0, 2), 16) / 255
    var g = parseInt(m[1].substr(2, 2), 16) / 255
    var b = parseInt(m[1].substr(4, 2), 16) / 255
    var max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min
    var h = hue
    if (d > 0) {
      if (max === r) h = ((g - b) / d) / 6
      else if (max === g) h = ((b - r) / d + 2) / 6
      else h = ((r - g) / d + 4) / 6
      if (h < 0) h += 1
    }
    hue = h
    saturation = max > 0 ? d / max : 0
    value = max
    alpha = m[2] ? parseInt(m[2], 16) / 255 : 1
    return true
  }
  function changed() { edited(color) }

  implicitWidth: Style.space(300)
  implicitHeight: Style.space(260)

  ColumnLayout {
    anchors.fill: parent
    spacing: Style.spacing.lg

    RowLayout {
      spacing: Style.spacing.md
      Layout.fillWidth: true
      Layout.fillHeight: true

      // ---- saturation / value square
      Item {
        id: square
        Layout.fillWidth: true
        Layout.fillHeight: true
        activeFocusOnTab: true

        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          color: { var c = root.hsvToRgb(root.hue, 1, 1); return Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, 1) }
        }
        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#ffffffff" }
            GradientStop { position: 1; color: "#00ffffff" }
          }
        }
        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          gradient: Gradient {
            GradientStop { position: 0; color: "#00000000" }
            GradientStop { position: 1; color: "#ff000000" }
          }
        }
        // Clamped inside the SV square with a concentric two-tone ring (background + foreground)
        // so it never clips off the edge, never collides with the Hue strip, and stays crisp over
        // both white and black corners in dark and light themes.
        Rectangle {
          width: root.knobSize
          height: root.knobSize
          radius: width / 2
          x: Math.max(0, Math.min(square.width - width, Math.round(root.saturation * square.width - width / 2)))
          y: Math.max(0, Math.min(square.height - height, Math.round((1 - root.value) * square.height - height / 2)))
          color: Commons.Color.background
          scale: root.hasCursor || square.activeFocus || squareArea.pressed ? 1.15 : 1

          Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: Commons.Color.foreground

            Rectangle {
              anchors.fill: parent
              anchors.margins: 1
              radius: width / 2
              color: Qt.rgba(root.rgb[0] / 255, root.rgb[1] / 255, root.rgb[2] / 255, 1)
            }
          }
        }
        MouseArea {
          id: squareArea
          anchors.fill: parent
          preventStealing: true
          cursorShape: Qt.CrossCursor
          function pick(mouse) {
            root.saturation = Math.max(0, Math.min(1, mouse.x / square.width))
            root.value = 1 - Math.max(0, Math.min(1, mouse.y / square.height))
            root.changed()
          }
          onPressed: function(mouse) { square.forceActiveFocus(); pick(mouse) }
          onPositionChanged: function(mouse) { if (pressed) pick(mouse) }
        }
        Keys.onPressed: function(event) {
          var step = (event.modifiers & Qt.ShiftModifier) ? 0.1 : 0.01
          if (event.key === Qt.Key_Left) root.saturation = Math.max(0, root.saturation - step)
          else if (event.key === Qt.Key_Right) root.saturation = Math.min(1, root.saturation + step)
          else if (event.key === Qt.Key_Up) root.value = Math.min(1, root.value + step)
          else if (event.key === Qt.Key_Down) root.value = Math.max(0, root.value - step)
          else return
          root.changed()
          event.accepted = true
        }
      }

      // ---- hue strip
      Strip {
        kind: "hue"
        fraction: root.hue
        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          gradient: Gradient {
            GradientStop { position: 0 / 6; color: "#ff0000" }
            GradientStop { position: 1 / 6; color: "#ffff00" }
            GradientStop { position: 2 / 6; color: "#00ff00" }
            GradientStop { position: 3 / 6; color: "#00ffff" }
            GradientStop { position: 4 / 6; color: "#0000ff" }
            GradientStop { position: 5 / 6; color: "#ff00ff" }
            GradientStop { position: 6 / 6; color: "#ff0000" }
          }
        }
        onPicked: function(f) { root.hue = Math.min(f, 0.9999); root.changed() }
      }

      // ---- alpha strip: opaque at the top
      Strip {
        kind: "alpha"
        visible: root.showAlpha
        fraction: 1 - root.alpha
        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          color: Commons.Color.track
        }
        Rectangle {
          anchors.fill: parent
          radius: root.smallRadius
          gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(root.rgb[0] / 255, root.rgb[1] / 255, root.rgb[2] / 255, 1) }
            GradientStop { position: 1; color: Qt.rgba(root.rgb[0] / 255, root.rgb[1] / 255, root.rgb[2] / 255, 0) }
          }
        }
        onPicked: function(f) { root.alpha = 1 - f; root.changed() }
      }
    }

    // ---- preview and hex
    RowLayout {
      spacing: Style.spacing.md
      Layout.fillWidth: true

      Rectangle {
        Layout.preferredWidth: hexField.implicitHeight
        Layout.preferredHeight: hexField.implicitHeight
        radius: Style.cornerRadius
        color: root.color
        border.width: 1
        border.color: Style.normalBorderColor
      }
      TextField {
        id: hexField
        Layout.fillWidth: true
        text: root.hex
        onTextEdited: if (root.setColor(text.trim())) root.changed()
      }
    }

    // ---- the theme's palette
    Row {
      spacing: Style.spacing.md
      Layout.fillWidth: true
      Repeater {
        model: root.swatches
        delegate: Rectangle {
          id: swatch
          required property string modelData
          readonly property bool chosen: modelData === root.hex.substr(0, 7)
          readonly property bool hot: swatchArea.containsMouse || activeFocus
          width: root.swatchSize
          height: root.swatchSize
          radius: root.smallRadius
          color: modelData
          border.width: chosen ? Math.max(1, Style.space(2)) : 1
          border.color: chosen ? Commons.Color.foreground : (hot ? Style.hoverBorderColor : Style.normalBorderColor)
          activeFocusOnTab: true
          function choose() {
            var a = root.alpha
            root.setColor(modelData)
            root.alpha = a
            root.changed()
          }
          MouseArea {
            id: swatchArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: swatch.choose()
          }
          Keys.onReturnPressed: choose()
          Keys.onSpacePressed: choose()
        }
      }
    }
  }

  // A vertical strip with a Slider knob; children draw the strip itself.
  component Strip: Item {
    id: strip
    property string kind: ""
    property real fraction: 0
    default property alias content: body.data
    signal picked(real fraction)

    Layout.preferredWidth: root.stripWidth
    Layout.fillHeight: true
    activeFocusOnTab: true

    Item { id: body; anchors.fill: parent }
    readonly property real _pad: Style.space(2)
    readonly property real _knob: Math.max(8, root.stripWidth - _pad * 2)
    Rectangle {
      width: strip._knob
      height: strip._knob
      radius: width / 2
      x: Math.round((strip.width - width) / 2)
      y: strip._pad + Math.round(strip.fraction * Math.max(0, strip.height - height - strip._pad * 2))
      color: Commons.Color.background
      scale: strip.activeFocus || stripArea.pressed ? 1.15 : 1

      Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: width / 2
        color: Commons.Color.foreground
      }
    }
    MouseArea {
      id: stripArea
      anchors.fill: parent
      preventStealing: true
      cursorShape: Qt.PointingHandCursor
      function pick(mouse) {
        var span = Math.max(1, strip.height - strip._knob - strip._pad * 2)
        strip.picked(Math.max(0, Math.min(1, (mouse.y - strip._pad - strip._knob / 2) / span)))
      }
      onPressed: function(mouse) { strip.forceActiveFocus(); pick(mouse) }
      onPositionChanged: function(mouse) { if (pressed) pick(mouse) }
    }
    Keys.onPressed: function(event) {
      var step = (event.modifiers & Qt.ShiftModifier) ? 0.1 : 0.01
      if (event.key === Qt.Key_Up) strip.picked(Math.max(0, strip.fraction - step))
      else if (event.key === Qt.Key_Down) strip.picked(Math.min(1, strip.fraction + step))
      else return
      event.accepted = true
    }
  }
}
