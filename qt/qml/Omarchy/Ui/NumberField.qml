import QtQuick
import QtQuick.Controls as QQC
import Omarchy.Theme
import Omarchy.Theme as Commons

Column {
  id: root

  property string label: ""
  property int value: 0
  property int from: 0
  property int to: 100
  property int stepSize: 1
  property color foreground: Commons.Color.foreground
  property color accent: Commons.Color.accent
  property string fontFamily: Style.font.family
  property real fontSize: Style.font.body
  property real fieldWidth: Style.spacing.numberFieldWidth
  property bool hasCursor: false
  property bool _hovered: false
  property alias field: spin

  signal modified(int value)
  signal hovered(bool on)

  spacing: Style.spacing.labelGap

  Text {
    textFormat: Text.PlainText
    visible: root.label !== ""
    text: root.label
    color: Commons.Color.textDim
    font.family: root.fontFamily
    font.pixelSize: Style.font.caption
    font.bold: true
  }

  QQC.SpinBox {
    id: spin
    width: root.fieldWidth
    implicitHeight: Math.max(Style.spacing.controlHeight, root.fontSize + Style.spacing.controlPaddingY * 2)
    from: root.from
    to: root.to
    stepSize: root.stepSize
    value: root.value
    editable: true
    font.family: root.fontFamily
    font.pixelSize: root.fontSize

    readonly property bool _focused: spin.activeFocus
    readonly property bool _hot: root._hovered || root.hasCursor
    readonly property var _borderSpec: Border.controlSpec(_focused ? "focus" : (_hot ? "hover-cursor" : "normal"), root.foreground, root.accent)

    leftPadding: (down.indicator ? down.indicator.width : 0) + Border.left(_borderSpec) + Style.spacing.sm
    rightPadding: (up.indicator ? up.indicator.width : 0) + Border.right(_borderSpec) + Style.spacing.sm
    topPadding: Border.top(_borderSpec)
    bottomPadding: Border.bottom(_borderSpec)

    onValueModified: root.modified(value)

    background: BorderSurface {
      color: Style.controlFill(spin._focused, spin._hot, root.foreground, root.accent)
      borderSpec: spin._borderSpec
      radius: Style.cornerRadius

      HoverHandler {
        onHoveredChanged: {
          root._hovered = hovered
          root.hovered(hovered)
        }
      }
    }

    contentItem: TextInput {
      text: spin.displayText
      font: spin.font
      color: root.foreground
      selectionColor: Style.selectionFillFor(root.foreground, root.accent)
      selectedTextColor: root.foreground
      horizontalAlignment: Qt.AlignHCenter
      verticalAlignment: Qt.AlignVCenter
      readOnly: !spin.editable
      validator: spin.validator
      inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    // The step buttons, drawn from the kit's tokens instead of the platform style (which paints
    // light blocks outside the shell): a glyph in the foreground, hover and pressed fills.
    component StepIndicator: Rectangle {
      required property var control
      required property string glyph
      readonly property real inset: Border.top(spin._borderSpec)
      y: inset
      width: spin.height - inset * 2
      height: width
      radius: Math.max(0, Style.cornerRadius - inset)
      color: control.pressed ? Style.pressedFillFor(root.foreground, root.accent)
        : control.hovered ? Style.hoverFillFor(root.foreground, root.accent)
        : "transparent"
      Text {
        anchors.centerIn: parent
        text: parent.glyph
        color: root.foreground
        opacity: parent.enabled ? 1 : 0.4
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
      }
    }

    down.indicator: StepIndicator {
      control: spin.down
      glyph: "\u2212"
      x: spin.mirrored ? spin.width - width - inset : inset
      enabled: spin.value > spin.from
    }

    up.indicator: StepIndicator {
      control: spin.up
      glyph: "+"
      x: spin.mirrored ? inset : spin.width - width - inset
      enabled: spin.value < spin.to
    }
  }
}
